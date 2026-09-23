# Blob, Buf and the typed buffers — semantics sheet

Provenance: Rakudo tag `2026.08`, file `src/core.c/Buf.rakumod` (1,804
lines: the `Blob` and `Buf` roles, the generated signed and unsigned
halves, `blob8`…`buf64`, `utf8`/`utf16`/`utf32`, and the `~`, `~&`,
`~|`, `~^`, `eqv`, `cmp`, `eq`…`ge` and `subbuf-rw` candidates), read
in full on 2026-09-23; `Encoding.rakumod` (8), `Encoding/Registry.rakumod`
(73), `Encoding/Builtin.rakumod` (54), `Encoding/Encoder/Builtin.rakumod`
(37), `Encoding/Decoder/Builtin.rakumod` (102), the `Str.encode` candidate
in `Str.rakumod`, `Cool.encode`, the encoding-name normaliser in
`Rakudo/Internals.rakumod`, and the `X::Buf::AsStr`, `X::Buf::Pack`,
`X::Encoding::Unknown`, `X::Encoding::AlreadyRegistered`, `X::Experimental`
and `X::TypeCheck` classes in `Exception.rakumod`. Out of scope: IO handles
(the IO sheet), `Supply.encode`/`.decode`, the JVM and JS branches, and
the experimental `pack`/`unpack` beyond their gate. Oracle: Homebrew
Rakudo v2026.08 on macOS (arm64, little-endian). Compared against Raku++
4.0.1-84-ga4291988 (build-arm64, 2026-09-23). Format and legend:
[README.md](README.md).

Where this sits against the declared spec: Rakudo passes all of
`S03-buf/read-int.t`, `read-num.t`, `read-write-bits.t`, `write-int.t`,
`write-num.t`, `S03-operators/buf.t`, `S32-container/buf.t`,
`S32-encoding/decoder.t`, `encoder.t`, `S32-str/encode.t` and
`windows-1251-windows-1252-encode-decode.t` here (11 files); Raku++ passes
the five `S03-buf` files only. The rules below are the ones behind those
files and behind the code that uses buffers in the wild: which values a
typed buffer stores and how an out-of-range one wraps, what is immutable
and what is a live reference, what `~`, `eqv`, `cmp`, `==` and `~~` mean
between buffers, which encodings exist under which names and what a
malformed byte does, and how the `read-*`/`write-*` family addresses
bytes. 21 of the 37 items are neither fully documented nor fully
asserted by Roast. Nine Rakudo behaviours are recorded as bugs (four of
them hangs) and eight as quirks; step two should not imitate the bugs.

Every probe ran with `alarm 10`, standard input closed, in a fresh sandbox
directory per engine. Non-ASCII characters are written as `\x[..]` escapes
in the probes, and bytes are printed as `.list` or `.raku`, so the sheet
is pure ASCII. Where a probe is expected to hang, the recorded output is
the alarm marker and the verification treats it as "no output within 10
seconds". `E` in a probe is a helper that reports `T:type` for a throw,
`F:type` for a returned Failure and `ok:value.raku` otherwise.

## A. Types and construction

### BB-01  The type family                                        D:partial R:partial V:quirk
`Blob` and `Buf` are parameterised roles with `uint8` as the default
element type; `blob8`, `blob16`, `blob32`, `blob64`, `buf8`…`buf64` are
constants naming `Blob[uint8]`…`Buf[uint64]` (they are `===` to the
parameterisation, and `.^name` is the long form). `utf8`, `utf16` and
`utf32` are classes doing `Blob[uint8]`, `Blob[uint16]`, `Blob[uint32]`;
a `utf8` is a `blob8` but not a `Buf`. `Buf` does `Blob`, not the other
way round. A buffer is `Positional` and `Stringy`, not `Iterable`, not
`List`; an instance is `Positional[uint8]` and is not `Cool` (the type
object alone answers True to `~~ Cool`). `.of` is the element type. The
plain `Blob`/`Buf` and their `[uint8]` parameterisations are DIFFERENT
types: `Blob.new(1) ~~ blob8` is False, `.WHAT === Blob` is False for an
instance (its WHAT is the punned class, whose name is still `Blob`), and
the MRO of an instance is `Buf`, `Any`, `Mu` (no `Blob` class in it — the
role is mixed in). A non-native element type (`Int`, `num32`, `Str`,
`Bool`, `Any`) is accepted by the parameterisation itself but every use of
it — `.new`, `.allocate`, `.elems`, `.of` — throws `X::Role::Instantiation`.
`int` and `uint` are accepted and store 64 bits.
```
say Blob.^name, " ", blob8.^name, " ", Blob[uint8].^name, " ", (blob8 === Blob[uint8]), " ", (buf8 === Buf[uint8]), " ", (Blob === Blob[uint8]), " ", (Buf === buf8), " ", Blob.of.^name, " ", blob16.of.^name, " ", Buf[int32].of.^name, " ", utf8.^name, " ", (utf8 ~~ Blob), " ", (utf8 ~~ blob8), " ", (utf8 ~~ Buf), " ", (Buf ~~ Blob), " ", (Blob ~~ Buf), " ", (buf8 ~~ Blob), " ", (Buf ~~ Stringy), " ", (Buf ~~ Positional), " ", (Blob ~~ Iterable), " ", (Blob ~~ List), " ", (Blob ~~ Cool), " ", (Blob ~~ Positional[uint8]), " ", (Blob.new(1) ~~ Cool), " ", (Blob.new(1) ~~ Positional[uint8]), " ", Buf.new.^mro.map(*.^name).raku, " ", utf16.of.^name, " ", utf32.of.^name, " ", Blob[int].^name, " ", Blob[uint].^name, " ", Buf[int64].^name, " ", Blob.new.^name, " ", blob8.new.^name, " ", (Blob.new.WHAT === Blob), " ", (blob8.new.WHAT === blob8), " ", (Blob.new(1) ~~ blob8), " ", (blob8.new(1) ~~ Blob), " ", Blob[int8].new.^name, " ", Buf.new.^name, " ", (Blob.new.WHAT === Blob.new(1).WHAT), " ", Blob.HOW.^name, " ", blob8.HOW.^name, " ", utf8.HOW.^name
# rakudo 2026.08: Blob Blob[uint8] Blob[uint8] True True False False uint8 uint16 int32 utf8 True True False True False True True True False False True False False True ("Buf", "Any", "Mu").Seq uint16 uint32 Blob[int] Blob[uint] Buf[int64] Blob Blob[uint8] False False False True Blob[int8] Buf True Perl6::Metamodel::ParametricRoleGroupHOW Perl6::Metamodel::CurriedRoleHOW Perl6::Metamodel::ClassHOW
say (try Blob[Int].^name) // $!.^name, " ", (try Blob[num32].^name) // $!.^name, " ", (try Blob[Str].^name) // $!.^name, " ", (try Buf[Rat].^name) // $!.^name, " ", (try Blob[Bool].^name) // $!.^name, " ", (try Blob[Any].^name) // $!.^name, " ", Blob[int8].^name, " ", Blob[uint16].^name, " ", Blob[int].of.^name, " ", (try Blob[Int].new(1).raku) // $!.^name, " ", (try Blob[num32].new.raku) // $!.^name, " ", (try Blob[Str].allocate(1).raku) // $!.^name, " ", (try Buf[Rat].new(1).raku) // $!.^name, " ", (try Blob[Bool].new(1).raku) // $!.^name, " ", (try Blob[Any].elems) // $!.^name, " ", Blob[int].new(1).raku, " ", Blob[uint].new(1).raku, " ", Blob[int64].new(-1).raku, " ", (try Blob[Int].of.^name) // $!.^name
# rakudo 2026.08: Blob[Int] Blob[num32] Blob[Str] Buf[Rat] Blob[Bool] Blob[Any] Blob[int8] Blob[uint16] int X::Role::Instantiation X::Role::Instantiation X::Role::Instantiation X::Role::Instantiation X::Role::Instantiation X::Role::Instantiation Blob[int].new(1) Blob[uint].new(1) Blob[int64].new(-1) X::Role::Instantiation
```
rakupp 4.0.1-84: differs — `blob8.^name` is `blob8`, `blob8 === Blob[uint8]` is False, `utf8 ~~ blob8` is False, `Blob.new(1) ~~ blob8` is True, the MRO is `Buf, Blob, Cool, Any, Mu` (so an instance IS `Cool`, and is NOT `Positional[uint8]`), `utf16.of`/`utf32.of` are `Mu`, `Blob[Int]`/`Blob[Str]`/`Blob[Bool]` silently become 8-bit buffers and `Blob[num32]` a 32-bit one, `Blob[Any].elems` is 1, and `.HOW` names differ (measured, not a target).

### BB-02  Construction                                            D:partial R:partial V:spec
`.new` takes nothing, a single Blob (copied, elements re-narrowed to the
new element width), a native `int` array, one Positional/Iterable, or a
slurpy list (nested lists and ranges flatten). Every element must be an
`Int` (Bools count, allomorphs from `<1 2 3>` count); a Str, a Rat, a Num,
a type object or a numeric string throws `X::TypeCheck` with `.operation`
"initializing element #N to Blob", `.got` the value and `.expected` the
element type — the index N is the position of the offending element. A
lazy list throws `X::Cannot::Lazy`. `.new` on an instance makes a fresh
buffer. `Buf.new(...)` is a plain `Buf`, `buf8.new(...)` a `Buf[uint8]`.
```
say Blob.new.elems, " ", Blob.new().raku, " ", Blob.new(1,2,3).raku, " ", Blob.new([1,2,3]).raku, " ", Blob.new((1,2,3)).raku, " ", Blob.new(1..3).raku, " ", Blob.new(^3).raku, " ", Blob.new(<1 2 3>).raku, " ", Blob.new(Buf.new(4,5)).raku, " ", Buf.new(Blob.new(6)).raku, " ", blob8.new(array[int].new(7,8)).raku, " ", Blob.new(True).raku, " ", Blob.new(1 xx 2).raku, " ", Blob.new((1,2),(3,4)).raku, " ", Blob.new([]).elems, " ", Blob.new(()).elems, " ", buf8.new(1..3).^name, " ", Buf.new(1..3).^name, " ", Blob.new(1..3).^name, " ", (try Blob.new(1..*)) // $!.^name, " ", (try Blob.new("abc")) // $!.^name, " ", (try Blob.new(1.5)) // $!.^name, " ", (try Blob.new(1, "x")) // $!.^name, " ", (try Blob.new(1e0)) // $!.^name, " ", (try Blob.new(Any)) // $!.^name, " ", (try Blob.new("7")) // $!.^name, " ", do { try Blob.new("abc"); my $e = $!; ((try $e.operation) // "-") ~ "/" ~ ((try $e.got.raku) // "-") ~ "/" ~ ((try $e.expected.^name) // "-") }, " ", do { try Blob.new(1, 2, "x"); my $e = $!; ((try $e.operation) // "-") }, " ", Blob.new(1..3).elems, " ", Blob.new(1, 2).new(3).raku, " ", utf8.new(blob8.new(97)).raku, " ", blob8.new(utf8.new(97)).raku, " ", (try Blob.new(blob16.new(300)).raku) // $!.^name, " ", (try buf16.new(Blob[int8].new(-1)).list.raku) // $!.^name, " ", Buf.new(Blob[int8].new(-1)).list.raku, " ", Blob.new(1, 2, 3).elems, " ", (try Blob.new(1..3, 5).raku) // $!.^name, " ", Blob.new((1..3).Seq).raku, " ", Blob.new((1..3).map(* * 2)).raku
# rakudo 2026.08: 0 Blob.new() Blob.new(1,2,3) Blob.new(1,2,3) Blob.new(1,2,3) Blob.new(1,2,3) Blob.new(0,1,2) Blob.new(1,2,3) Blob.new(4,5) Buf.new(6) Blob[uint8].new(7,8) Blob.new(1) Blob.new(1,1) Blob.new(1,2,3,4) 0 0 Buf[uint8] Buf Blob X::Cannot::Lazy X::TypeCheck X::TypeCheck X::TypeCheck X::TypeCheck X::TypeCheck X::TypeCheck initializing element #0 to Blob/"abc"/uint8 initializing element #2 to Blob 3 Blob.new(3) utf8.new(97) Blob[uint8].new(97) Blob.new(44) (65535,) (255,) 3 Blob.new(1,2,3,5) Blob.new(1,2,3) Blob.new(2,4,6)
```
rakupp 4.0.1-84: differs — `buf8.new(1..3).^name` is `Buf`, and `Blob.new(1..*)` does not throw: it builds a large buffer whose gist filled the output, so the rest of the line is unmeasured.

### BB-03  Storage widths and wrap-around                           D:no R:no V:bug
An Int outside the element's range is stored modulo the width: `256` in
an 8-bit unsigned buffer reads back 0, `-1` reads 255, `200` in `int8`
reads -56, `65536` in `uint16` reads 0, `2**32` in `uint32` 0, `-1` in
`uint64` 18446744073709551615, `-5` in `Blob[uint]` 18446744073709551611;
a value that does not fit a native 64-bit integer at all (`2**64` in
`uint64`, `2**63` in `int64`, `2**70`) throws `X::AdHoc`. Elements read
back as `Int`. `.bytes` is elems times the element width — 1, 2, 4, 8 —
but for `Blob[int]` and `Blob[uint]` it is elems times 1, although the
gist shows 16 hex digits per element: that is the bug (docs: "the number
of bytes used by the elements").
```
say blob8.new(256).list.raku, " ", blob8.new(-1).list.raku, " ", blob8.new(300, 511, 65535).list.raku, " ", Blob[int8].new(200).list.raku, " ", Blob[int8].new(-129).list.raku, " ", Blob[int8].new(127, 128).list.raku, " ", blob16.new(65536, 65537).list.raku, " ", blob16.new(-1).list.raku, " ", Blob[int16].new(40000).list.raku, " ", blob32.new(2**32, 2**32 + 5).list.raku, " ", blob32.new(-1).list.raku, " ", Blob[int32].new(2**31).list.raku, " ", blob64.new(-1).list.raku, " ", blob64.new(2**64 - 1).list.raku, " ", (try blob64.new(2**64).list.raku) // $!.^name, " ", (try Blob[int64].new(2**63).list.raku) // $!.^name, " ", Blob[int64].new(-2**63).list.raku, " ", Blob[int].new(-5).list.raku, " ", Blob[uint].new(-5).list.raku, " ", buf8.new(1).bytes, " ", buf16.new(1).bytes, " ", buf32.new(1).bytes, " ", buf64.new(1).bytes, " ", Blob[int].new(1).bytes, " ", Blob[uint].new(1).bytes, " ", Blob.new.bytes, " ", (try blob8.new(2**70).list.raku) // $!.^name, " ", Blob[int8].new(-1)[0], " ", blob8.new(-1)[0], " ", Blob[int8].new(-1)[0].^name, " ", blob64.new(2**64 - 1)[0].^name
# rakudo 2026.08: (0,) (255,) (44, 255, 255) (-56,) (127,) (127, -128) (0, 1) (65535,) (-25536,) (0, 5) (4294967295,) (-2147483648,) (18446744073709551615,) (18446744073709551615,) X::AdHoc X::AdHoc (-9223372036854775808,) (-5,) (18446744073709551611,) 1 2 4 8 1 1 0 X::AdHoc -1 255 Int Int
```
rakupp 4.0.1-84: differs — the three too-wide values wrap (`(0,)`, `-9223372036854775808`, `(0,)`) instead of throwing, and `Blob[uint].new(-5)` stores 251 (an 8-bit `uint`); `.bytes` of `Blob[int]` is 1 here too.

### BB-04  allocate                                                 D:yes R:yes V:bug
`allocate(n)` gives n zeros; `allocate(n, value)` fills with the Int
(wrapped to the width); `allocate(n, pattern)` repeats a list, native int
array or Blob cyclically and truncates to n; `allocate(0, ...)` is empty.
A pattern element or fill value that is not an Int gives a Failure
`X::TypeCheck` (`.operation` "allocate to Blob") for a single value and a
THROWN `X::TypeCheck` for a bad element inside a list; a negative count
throws `X::AdHoc`; a non-Int count, a type object as fill, or `allocate`
on an instance is `X::Multi::NoMatch`; a fill too wide for a native int
throws `X::AdHoc`. An EMPTY pattern hangs (bug): `Blob.allocate(3, ())`
never returns.
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; say Blob.allocate(3).raku, " ", Buf.allocate(2).^name, " ", Blob.allocate(0).elems, " ", Blob.allocate(3, 42).raku, " ", Blob.allocate(3, 256).raku, " ", Blob.allocate(3, -1).raku, " ", Blob.allocate(5, (1,2)).raku, " ", Blob.allocate(5, [7]).raku, " ", Blob.allocate(4, Blob.new(1,2,3)).raku, " ", Blob.allocate(2, Blob.new(1,2,3)).raku, " ", Blob.allocate(4, array[int].new(9,8)).raku, " ", blob16.allocate(2, 70000).list.raku, " ", Blob[int8].allocate(2, 200).list.raku, " ", E({ Blob.allocate(2, "x") }), " ", E({ Blob.allocate(2, 1.5) }), " ", E({ Blob.allocate(2, (1, "x")) }), " ", E({ Blob.allocate(-1).elems }), " ", E({ Blob.allocate(2.7).elems }), " ", E({ Blob.new.allocate(2).raku }), " ", (try utf8.allocate(2, 97).Str) // $!.^name, " ", Blob.allocate(3, 1..2).raku, " ", Blob.allocate(3, (1,2,3,4)).raku, " ", Blob.allocate(0, (1,2)).raku, " ", Blob.allocate(3, True).raku, " ", do { try Blob.allocate(2, "x"); my $e = $!; ((try $e.operation) // "-") ~ "/" ~ ((try $e.got.raku) // "-") ~ "/" ~ ((try $e.expected.^name) // "-") }, " ", E({ Blob.allocate(2, 2**70).list.raku }), " ", E({ Blob.allocate(2, Any) }), " ", E({ Blob.allocate("3") })
# rakudo 2026.08: Blob.new(0,0,0) Buf 0 Blob.new(42,42,42) Blob.new(0,0,0) Blob.new(255,255,255) Blob.new(1,2,1,2,1) Blob.new(7,7,7,7,7) Blob.new(1,2,3,1) Blob.new(1,2) Blob.new(9,8,9,8) (4464, 4464) (-56, -56) F:X::TypeCheck F:X::TypeCheck T:X::TypeCheck T:X::AdHoc T:X::Multi::NoMatch T:X::Multi::NoMatch aa Blob.new(1,2,1) Blob.new(1,2,3) Blob.new() Blob.new(1,1,1) allocate to Blob/"x"/uint8 T:X::AdHoc T:X::Multi::NoMatch T:X::Multi::NoMatch
say Blob.allocate(3, ()).raku
# rakudo 2026.08: (no output: killed by the 10-second alarm)
```
rakupp 4.0.1-84: differs — a Blob pattern is not spread (`allocate(4, Blob.new(1,2,3))` is `3,3,3,3`), a 16-bit or signed fill wraps to zero, a bad fill throws instead of failing, a Rat fill/count truncates, `utf8.allocate(2, 97).Str` is `X::Buf::AsStr`, and the empty pattern returns `Blob.new(0,0,0)` (the right answer).

## B. Printing, identity, comparison, numeric context

### BB-05  gist, raku, and the refusal to be a Str                  D:partial R:yes V:spec
`.gist` is the type name, `:0x<`, the elements as upper-case hex of the
element width (2, 4, 8, 16 digits; negative signed elements in two's
complement), `>`; the plain `Blob`/`Buf` print as `Blob:0x<..>`, the
parameterised ones as `Blob[uint8]:0x<..>`, `utf8` as `utf8:0x<..>`.
`.raku` is `Name.new(1,2)` with no spaces. `.Str`, `.Stringy`, prefix
`~`, string interpolation, `.chars` and `.codes` throw `X::Buf::AsStr`
whose `.object` is the buffer and `.method` the name called ("Str",
"Stringy", "chars"); the Str methods a Cool would have (`uc`, `lc`,
`comb`, `substr`, `ord`) do not exist (`X::Method::NotFound`), and `.fmt`
is `X::Multi::NoMatch`.
```
say Blob.new(1,2,255).gist, " ", Buf.new(1,2).gist, " ", blob8.new(1).gist, " ", buf8.new(1).gist, " ", utf8.new(97,98).gist, " ", utf16.new(97).gist, " ", blob16.new(1).gist, " ", blob32.new(1).gist, " ", blob64.new(1).gist, " ", Blob[int].new(1).gist, " ", Blob.new.gist, " ", Blob.new(1,2).raku, " ", Buf.new(1,2).raku, " ", buf8.new(1,2).raku, " ", blob8.new(1,2).raku, " ", utf8.new(97).raku, " ", Blob[int8].new(-1).raku, " ", Blob.new.raku, " ", Blob.gist, " ", Blob.raku, " ", buf8.gist, " ", do { try Blob.new(1).Str; my $e = $!; $e.^name ~ ":" ~ ((try $e.method) // "-") }, " ", do { try Blob.new(1).Stringy; my $e = $!; $e.^name ~ ":" ~ ((try $e.method) // "-") }, " ", do { try ~Blob.new(1); my $e = $!; $e.^name ~ ":" ~ ((try $e.method) // "-") }, " ", (try "x{Blob.new(1)}") // $!.^name, " ", do { try Blob.new(1).chars; my $e = $!; $e.^name ~ ":" ~ ((try $e.method) // "-") }, " ", (try Blob.new(1).codes) // $!.^name, " ", (try Blob.new(1).uc) // $!.^name, " ", (try Blob.new(1).fmt("%s")) // $!.^name, " ", (try Blob.new(1).Str.^name) // $!.^name, " ", (try Buf.new(1).Stringy) // $!.^name, " ", do { try Blob.new(1).Str; ((try $!.object.raku) // "-") }, " ", Blob.new(1).gist.^name, " ", (try Blob.new(1).comb) // $!.^name, " ", (try Blob.new(1).substr(0)) // $!.^name, " ", (try Blob.new(1).ord) // $!.^name, " ", (try Blob.new(1).lc) // $!.^name
# rakudo 2026.08: Blob:0x<01 02 FF> Buf:0x<01 02> Blob[uint8]:0x<01> Buf[uint8]:0x<01> utf8:0x<61 62> utf16:0x<0061> Blob[uint16]:0x<0001> Blob[uint32]:0x<00000001> Blob[uint64]:0x<0000000000000001> Blob[int]:0x<0000000000000001> Blob:0x<> Blob.new(1,2) Buf.new(1,2) Buf[uint8].new(1,2) Blob[uint8].new(1,2) utf8.new(97) Blob[int8].new(-1) Blob.new() (Blob) Blob (Buf[uint8]) X::Buf::AsStr:Str X::Buf::AsStr:Stringy X::Buf::AsStr:Stringy X::Buf::AsStr X::Buf::AsStr:chars X::Buf::AsStr X::Method::NotFound X::Multi::NoMatch X::Buf::AsStr X::Buf::AsStr Blob.new(1) Str X::Method::NotFound X::Method::NotFound X::Method::NotFound X::Method::NotFound
```
rakupp 4.0.1-84: differs — `utf8`/`utf16` print under their base type, `Blob[int]` prints as `Blob[int8]`, `buf8.gist` is `(buf8)`, `.Stringy` reports method `Str`, prefix `~`, interpolation, `.chars`, `.codes`, `.uc`, `.lc`, `.comb`, `.substr` and `.ord` answer instead of throwing, and `X::Buf::AsStr` has no `.object`.

### BB-06  WHICH, ===, eqv, eq, ==, and hash keys                    D:no R:partial V:spec
A Blob is a value type: `.WHICH` is a `ValueObjAt` made of the type name
and a digest of the bytes, so two equal Blobs are `===`, `unique`, `Set`
and object-hash keys treat them as one; a Buf's WHICH is an `ObjAt` and
two equal Bufs are distinct. `eqv` needs the SAME type and equal
elements: `Blob.new(1,2) eqv blob8.new(1,2)` is False, as is `Buf eqv
buf8`, `Buf eqv Blob`, `utf8 eqv blob8`; `eq`/`ne` compare elements
across types and widths (`Blob eq Buf eq blob8`), by value (an `int8`
`-1` is not `eq` a `uint8` `255`). `==`/`!=` compare `.Numeric`, the
element COUNT; `<` is `X::Multi::NoMatch`. A Blob as an ordinary hash
key throws `X::Buf::AsStr`; a `utf8` key is its decoded text.
```
say Blob.new(1,2).WHICH.^name, " ", Blob.new(1,2).WHICH.Str.subst(/\|.*/, "|..."), " ", blob8.new(1).WHICH.Str.subst(/\|.*/, "|..."), " ", utf8.new(1).WHICH.Str.subst(/\|.*/, "|..."), " ", Buf.new(1).WHICH.^name, " ", (Blob.new(1,2) === Blob.new(1,2)), " ", (Blob.new(1,2) === Blob.new(1,3)), " ", (Buf.new(1) === Buf.new(1)), " ", (Blob.new(1) === blob8.new(1)), " ", (Blob.new(1,2) eqv Blob.new(1,2)), " ", (Blob.new(1,2) eqv blob8.new(1,2)), " ", (Buf.new(1) eqv buf8.new(1)), " ", (Buf.new(1) eqv Blob.new(1)), " ", (Buf.new(1) eqv Buf.new(1)), " ", ("a".encode eqv utf8.new(97)), " ", ("a".encode eqv blob8.new(97)), " ", (Blob.new(1) eq Buf.new(1)), " ", (Blob.new(1) eq blob8.new(1)), " ", (Blob.new(1,2) ne Blob.new(1,2)), " ", (Blob.new(1,2) == Blob.new(3,4)), " ", (Blob.new(1,2) == Blob.new(1)), " ", (Blob.new(1,2) != Blob.new(3,4)), " ", (try Blob.new(1) < Blob.new(1,2)) // $!.^name, " ", (Blob eqv Blob), " ", (Buf eqv Blob), " ", (Blob.new(1,2), Blob.new(1,2)).unique.elems, " ", (Buf.new(1), Buf.new(1)).unique.elems, " ", (Blob.new(1), Blob.new(1)).Set.elems, " ", do { my %h{Any}; %h{Blob.new(1)} = 1; %h{Blob.new(1)} = 2; %h.elems }, " ", do { my %h{Any}; %h{Buf.new(1)} = 1; %h{Buf.new(1)} = 2; %h.elems }, " ", (try do { my %h; %h{Blob.new(1)} = 1; %h.keys.raku }) // $!.^name, " ", (try do { my %h; %h{utf8.new(97)} = 1; %h.keys.raku }) // $!.^name, " ", (Blob.new(1,2) === Blob.new(1,2)).^name, " ", (Blob.new(1,2) eqv Blob.new(1,2,0)), " ", (blob8.new(1) eqv blob8.new(1)), " ", (Blob[int8].new(-1) eq blob8.new(255)), " ", (Blob[int8].new(-1) eqv blob8.new(255)), " ", (try Blob.new(1,2) === Blob.new(1,2).Buf.Blob) // $!.^name, " ", ("ab".encode === "ab".encode), " ", (utf8.new(97) eqv utf8.new(97)), " ", do { my $b = Buf.new(1); $b === $b }, " ", do { my $b = Buf.new(1); $b eqv $b }, " ", (try Blob.new(1) eqv Blob.new(1).Buf.Blob) // $!.^name
# rakudo 2026.08: ValueObjAt Blob|... Blob[uint8]|... utf8|... ObjAt True False False False True False False False True True False True True False True False False X::Multi::NoMatch True False 1 2 1 1 2 X::Buf::AsStr ("a",).Seq Bool False True False False True True True True True True
```
rakupp 4.0.1-84: differs — `blob8` and `utf8` WHICHes carry the plain `Blob|` prefix, `eqv` and `===` ignore the type (`Blob === blob8`, `Buf eqv Blob` are True), an `int8` -1 is `eq` and `eqv` a `uint8` 255, a Blob hash key stringifies to its bytes, `<` works, and `.Buf` is missing.

### BB-07  cmp, lt, le, gt, ge between buffers                      D:no R:partial V:quirk
`cmp` (and `lt`…`ge`, `before`/`after`, `sort`, `max`) compares the
element COUNTS first and the elements only when the counts are equal:
`Blob.new(2) cmp Blob.new(1,9,9)` is Less, `Blob.new(1,3) gt
Blob.new(1,2,9)` is False, the empty buffer is Less than any other. The
two operands must be of the same type (`::?CLASS:D`): `blob8 cmp int8`,
`Buf cmp blob16`, `utf8 lt blob8` all throw
`X::TypeCheck::Binding::Parameter`, while `eq` between them works. With a
non-Blob on either side `eq`, `cmp`, `leg`, `lt` throw `X::Buf::AsStr`
and `<=>` is `X::Multi::NoMatch`; a `utf8` compares as its decoded Str.
Roast asserts only cases on which count-first and element-wise agree.
```
say (Blob.new(1,2,3) cmp Blob.new(1,2,3)), " ", (Blob.new(1,2,3) cmp Blob.new(1,2,3,4)), " ", (Blob.new(1,2,4) cmp Blob.new(1,2,3)), " ", (Blob.new(2) cmp Blob.new(1,9,9)), " ", (Blob.new cmp Blob.new(0)), " ", (Blob.new(1,2) lt Blob.new(1,2,3)), " ", (Blob.new(1,2) le Blob.new(1,2)), " ", (Blob.new(1,3) gt Blob.new(1,2,9)), " ", (Blob.new(1,2) ge Blob.new(1,2)), " ", (Blob[int8].new(-1) cmp Blob[int8].new(1)), " ", (try blob8.new(255) cmp Blob[int8].new(-1)) // $!.^name, " ", (try Buf.new(1) cmp blob16.new(1)) // $!.^name, " ", (try Blob.new(97) eq "a") // $!.^name, " ", (try Blob.new(97) cmp "a") // $!.^name, " ", (try Blob.new(1,2) leg Blob.new(1,2)) // $!.^name, " ", (utf8.new(97) eq "a"), " ", (utf8.new(97) cmp "b"), " ", (try Blob.new(1) lt 2) // $!.^name, " ", (try Blob.new(1,2,3) <=> Blob.new(4,5)) // $!.^name, " ", (Blob.new(1,2) eq Buf.new(1,2)), " ", (Blob.new(1,2) eqv Buf.new(1,2)), " ", ((Blob.new(1,2), Blob.new(1)).sort.raku), " ", ((Blob.new(1,2), Blob.new(1)).max.raku), " ", (Blob.new(1,2) before Blob.new(1,3)), " ", (Blob.new(1,2) after Blob.new(1,3)), " ", (Blob.new(1,2) cmp Blob.new(1,2)).^name, " ", (try blob16.new(300) cmp blob8.new(255)) // $!.^name, " ", (try Blob[int8].new(-1) cmp blob8.new(1)) // $!.^name, " ", (try Blob.new(1) eq 1) // $!.^name, " ", (Blob.new(1) ne Blob.new(2)), " ", (Blob.new(1) !eqv Blob.new(2)), " ", (utf8.new(98) gt "a"), " ", (try utf8.new(97) lt blob8.new(98)) // $!.^name, " ", (try blob8.new(97) lt utf8.new(98)) // $!.^name, " ", (utf8.new(97) eq blob8.new(97)), " ", (try utf8.new(97) cmp blob8.new(98)) // $!.^name
# rakudo 2026.08: Same Less More Less Less True True False True Less X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter X::Buf::AsStr X::Buf::AsStr X::Buf::AsStr True Less X::Buf::AsStr X::Multi::NoMatch True False (Blob.new(1), Blob.new(1,2)).Seq Blob.new(1,2) True False Order X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter X::Buf::AsStr True True True X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter True X::TypeCheck::Binding::Parameter
```
rakupp 4.0.1-84: differs — the order is element-wise (`Blob.new(2) cmp Blob.new(1,9,9)` is More), cross-type comparisons are allowed, and `eq`/`cmp`/`leg`/`lt`/`<=>` with a Str or Int answer instead of throwing.

### BB-08  Bool, Numeric, Int, and arithmetic                       D:partial R:partial V:spec
An empty buffer is False, any other True (`Blob.new(0)` is True). `+`,
`.Numeric`, `.Int` are the element count, and so is what `+`, `-`, `*`,
`**`, `div`, `+&`, `+|`, `==` and `~~ Int` see; `<`, `%`, `<=>` and
`.Real` fail with `X::Multi::NoMatch` (there is no `Real` coercion);
`.Num`, `.Rat`, `.succ`, `.sqrt`, `.Complex` do not exist; `max`/`min`
against a number throw `X::Buf::AsStr`. `.sum` adds the elements. `.end`
is elems-1, `.Capture` the elements. `.encoding` is `Any` for every
buffer except `utf8`/`utf16`/`utf32`, which answer `utf-8`, `utf-16`,
`utf-32`; a Blob returned by `encode("ascii")` answers `Any`.
```
say ?Blob.new, " ", ?Blob.new(0), " ", Blob.new(1,2).Bool, " ", +Blob.new(1,2,3), " ", Blob.new(1,2).Numeric, " ", Blob.new(1,2).Int, " ", Blob.new(5,5).elems, " ", Blob.elems, " ", (try Blob.new(1,2) + 1) // $!.^name, " ", (try Blob.new(1,2) * Blob.new(1,2,3)) // $!.^name, " ", (try -Blob.new(1)) // $!.^name, " ", Blob.new(1,2) == 2, " ", (try Blob.new(1,2) < 3) // $!.^name, " ", (try Blob.new(1,2) ** 2) // $!.^name, " ", (try Blob.new(1,2) div 2) // $!.^name, " ", (try Blob.new(1,2) % 2) // $!.^name, " ", Blob.new(1,2).Capture.raku, " ", Blob.new(1,2).end, " ", Blob.new(1,2,3).Numeric.^name, " ", (try Blob.new(1,2) +& 3) // $!.^name, " ", (try Blob.new(1,2,3) +| 4) // $!.^name, " ", (try Blob.encoding.raku) // $!.^name, " ", (try utf8.encoding) // $!.^name, " ", (try utf16.encoding) // $!.^name, " ", (try utf32.encoding) // $!.^name, " ", (try blob8.new.encoding.raku) // $!.^name, " ", (try "a".encode("ascii").encoding.raku) // $!.^name, " ", Blob.new(1,2).sum, " ", Blob.Bool, " ", Blob.defined, " ", Blob.new.defined, " ", ?Blob.new(0,0), " ", Blob.new(1,2).Numeric == 2, " ", (try Blob.new(1,2).Real) // $!.^name, " ", (try Blob.new(1,2).Num) // $!.^name, " ", (try Blob.new(1,2).Rat) // $!.^name, " ", Blob.new(1,2).Int.^name, " ", (try Blob.new(1,2) ~~ 2) // $!.^name, " ", (try Blob.new(1,2).succ) // $!.^name, " ", (try Blob.new(1,2) max 5) // $!.^name, " ", (try Blob.new(1,2).sqrt) // $!.^name, " ", (try Blob.new(1,2).Complex) // $!.^name, " ", Blob.new(1,2).Numeric.raku, " ", Blob.new(255).sum, " ", Blob[int8].new(-1, -2).sum
# rakudo 2026.08: False True True 3 2 2 2 1 3 6 -1 True X::Multi::NoMatch 4 1 X::Multi::NoMatch \(1, 2) 1 Int 2 7 Any utf-8 utf-16 utf-32 Any Any 3 False False True True True X::Multi::NoMatch X::Method::NotFound X::Method::NotFound Int True X::Method::NotFound X::Buf::AsStr X::Method::NotFound X::Method::NotFound 2 255 -3
```
rakupp 4.0.1-84: differs — the line died at `Blob.new(1,2).sum` (a Buf stringified to `""` and numified); before that `<`, `%`, `.Real` and the six `.encoding` calls are `X::Method::NotFound` where Rakudo answers, and the rest is unmeasured.

## C. Immutability and mutation

### BB-09  A Blob cannot be changed                                 D:partial R:partial V:spec
Assigning into an element, `ASSIGN-POS`, `STORE` on an existing Blob, and
assigning to a bound `@` view all throw `X::Assignment::RO`; assigning
beyond the end throws `X::OutOfRange`; `[0]++`, `push`, `append`,
`unshift`, `prepend`, `splice` are `X::Multi::NoMatch` (they exist on Any
without a Blob candidate); `pop`, `shift`, `reallocate`, `subbuf-rw` and
the `write-*` family are `X::Method::NotFound`; `:delete`/`DELETE-POS`
return a Failure `X::AdHoc`; binding into an element is `X::Bind`. Reading
past the end or with a negative index is a Failure `X::OutOfRange`; a
slice that reaches past the end THROWS `X::OutOfRange`. `.sort[0] = 5`,
`.list[0] = 5` and `for $b.list { $_ = 5 }` refuse too (the elements are
plain values). The variable itself can be reassigned.
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; my $b = Blob.new(1,2,3); my $i = -1; say E({ $b[0] = 5 }), " ", E({ $b[5] = 5 }), " ", E({ $b[0]++ }), " ", E({ $b.push(4) }), " ", E({ $b.append(4) }), " ", E({ $b.pop }), " ", E({ $b.shift }), " ", E({ $b.unshift(0) }), " ", E({ $b.prepend(0) }), " ", E({ $b.splice(0, 1) }), " ", E({ $b.reallocate(5) }), " ", E({ $b.subbuf-rw(0, 1) }), " ", E({ $b[0]:delete }), " ", E({ $b.STORE((7,8)) }), " ", E({ $b[$i] }), " ", E({ $b[3] }), " ", E({ $b[1..5].map({ $_ ~~ Failure ?? "F" !! $_ }).raku }), " ", E({ $b.write-uint8(0, 1) }), " ", E({ $b.write-ubits(0, 8, 1) }), " ", E({ $b.read-uint8(0) }), " ", E({ $b.Buf.push(4) }), " ", E({ my Blob $x .= new(1); $x[0] = 2 }), " ", E({ $b[0] := 9 }), " ", E({ $b.ASSIGN-POS(0, 9) }), " ", E({ $b.BIND-POS(0, 9) }), " ", E({ $b.DELETE-POS(0) }), " ", E({ $b[0] }), " ", $b.raku, " ", E({ $b = Blob.new(9) }), " ", $b.raku, " ", E({ $b.sort[0] = 5 }), " ", E({ $b.list[0] = 5 }), " ", E({ for $b.list { $_ = 5 }; $b.raku }), " ", E({ my @a := Blob.new(1); @a[0] = 2 })
# rakudo 2026.08: T:X::Assignment::RO T:X::OutOfRange T:X::Multi::NoMatch T:X::Multi::NoMatch T:X::Multi::NoMatch T:X::Method::NotFound T:X::Method::NotFound T:X::Multi::NoMatch T:X::Multi::NoMatch T:X::Multi::NoMatch T:X::Method::NotFound T:X::Method::NotFound F:X::AdHoc T:X::Assignment::RO F:X::OutOfRange F:X::OutOfRange T:X::OutOfRange T:X::Method::NotFound T:X::Method::NotFound ok:1 ok:Buf.new(1,2,3,4) T:X::Assignment::RO T:X::Bind T:X::Assignment::RO T:X::Bind F:X::AdHoc ok:1 Blob.new(1,2,3) ok:Blob.new(9) Blob.new(9) T:X::Assignment::RO T:X::Assignment::RO T:X::AdHoc T:X::Assignment::RO
```
rakupp 4.0.1-84: differs — the six list mutators are `X::Method::NotFound`, `[0]++` is `X::Assignment::RO`, `:delete` and the `write-*` methods throw a rakupp-only `X::Buf::RO`, `$b[3]` is `Any` and the slice truncates silently, `.Buf` is missing, and `for $b.list { $_ = 5 }` is accepted without effect.

### BB-10  Buf subscripts                                           D:partial R:partial V:spec
Assignment writes the Int modulo the width (`256` reads 0, `-1` 255,
True 1); a Str (even "7"), Rat, Num, Nil, type object or too-wide Int
throws `X::AdHoc`. Assigning past the end grows the buffer with zeros;
READING past the end gives 0 without growing (`$b[20]`, `$b[1..9]`,
`$b[2..3]`); a negative index is a Failure `X::OutOfRange` on read and on
write; `Inf` throws `X::Numeric::CannotConvert`. Slice assignment works;
`[*-1]` works; a fractional index truncates and a numeric string
converts; `:exists` is False past the end and for a negative index;
`:delete` is a Failure `X::AdHoc`; binding is `X::Bind`. An element is a
live reference (`UIntPosRef` for unsigned): `my $s := $b[0]; $s = 77`
changes the buffer, `++` and `+=` work in place (wrapping).
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; my $b = Buf.new(1,2,3); my $i = -1; say E({ $b[0] = 9 }), " ", $b.raku, " ", E({ $b[5] = 7 }), " ", $b.raku, " ", $b.elems, " ", E({ $b[$i] }), " ", E({ $b[$i] = 1 }), " ", E({ $b[*-1] }), " ", E({ $b[1,2].raku }), " ", E({ $b[1..2] = 5, 6; $b.raku }), " ", E({ $b[0] = 256; $b[0] }), " ", E({ $b[0] = -1; $b[0] }), " ", E({ $b[0] = "x" }), " ", E({ $b[0] = "7"; $b[0] }), " ", E({ $b[0] = 1.5 }), " ", E({ $b[0] = True; $b[0] }), " ", E({ $b[0] = Nil; $b[0] }), " ", E({ $b[0] = Int }), " ", ($b[0]:exists), " ", ($b[99]:exists), " ", ($b[$i]:exists), " ", E({ $b[1]:delete }), " ", E({ $b[20] }), " ", $b.elems, " ", E({ $b[1.7] }), " ", E({ $b["2"] }), " ", E({ $b[0]++; $b[0] }), " ", E({ $b[0] += 300; $b[0] }), " ", E({ $b[1..9].raku }), " ", E({ $b[*-1] = 42; $b[*-1] }), " ", E({ $b[^2].raku }), " ", E({ my $s := $b[0]; $s = 77; $b[0] }), " ", E({ $b[0] := 5 }), " ", E({ $b[0] = 2**70; $b[0] }), " ", E({ $b[0] = 1e0; $b[0] }), " ", E({ $b[9] = 1; $b.elems }), " ", E({ $b[0, 1] = 3, 4; $b[0, 1].raku }), " ", E({ $b[0]:p }), " ", E({ $b[$i]:v }), " ", E({ $b.AT-POS(0).VAR.^name }), " ", E({ Blob.new(1).AT-POS(0).VAR.^name }), " ", E({ $b[0]:delete }), " ", E({ $b[Inf] }), " ", E({ $b[2..0].raku }), " ", E({ $b[*].elems })
# rakudo 2026.08: ok:9 Buf.new(9,2,3) ok:7 Buf.new(9,2,3,0,0,7) 6 F:X::OutOfRange F:X::OutOfRange ok:7 ok:"(2, 3)" ok:"Buf.new(9,5,6,0,0,7)" ok:0 ok:255 T:X::AdHoc T:X::AdHoc T:X::AdHoc ok:1 T:X::AdHoc T:X::AdHoc True False False F:X::AdHoc ok:0 6 ok:5 ok:6 ok:2 ok:46 ok:"(5, 6, 0, 0, 7, 0, 0, 0, 0)" ok:42 ok:"(46, 5)" ok:77 T:X::Bind T:X::AdHoc T:X::AdHoc ok:10 ok:"(3, 4)" ok:0 => 3 ok:() ok:"UIntPosRef" ok:"Int" F:X::AdHoc T:X::Numeric::CannotConvert ok:"()" ok:10
```
rakupp 4.0.1-84: differs — a negative-index write throws instead of failing, a Str/Rat/Nil/type-object/too-wide value is stored as some number instead of throwing, `:exists` is True for a negative index, `:delete` is `Any`, `[1..9]` truncates instead of reading zeros, elements are not references (`$b[0]++` is `X::Assignment::RO`, a bound scalar does not write back), `:p` answers a Pair whose value is the whole buffer, and `$b[Inf]` is `Any`.

### BB-11  push, append, unshift, prepend, pop, shift               D:yes R:yes V:bug
All four adders return the buffer itself. `push`/`unshift` take Ints, a
Blob, or several values; a SINGLE non-Int argument — a list, an Array, a
Range, a Str, a Rat — is a Failure `X::TypeCheck` (`.operation` "push to
Buf", `.expected` the element type); `push(|@list)` works. `append`/
`prepend` additionally flatten one Positional (a list, Array, Range or
native int array); a lazy one is a Failure `X::Cannot::Lazy`. Values
wrap to the width. A BAD ELEMENT INSIDE A MULTI-VALUE CALL THROWS
`X::TypeCheck` AFTER THE BUFFER HAS GROWN: `push(1, "x")` on 27 elements
leaves 29 (the 1 and a zero slot) — the bug; an Int too wide for a
native throws `X::AdHoc` the same way. `pop`/`shift` return the element;
on an empty Buf they are a Failure `X::Cannot::Empty` (`.action` "pop",
`.what` "Buf"). `append(Buf.new)`, `append(())` and `push()` are no-ops.
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; my $b = Buf.new(1,2,3); say E({ $b.push(4).raku }), " ", E({ $b.push(5, 6).elems }), " ", E({ $b.push(Blob.new(7)).elems }), " ", E({ $b.push((8, 9)) }), " ", E({ $b.push([8, 9]) }), " ", E({ $b.push(1..2) }), " ", E({ $b.push(|(8, 9)).elems }), " ", E({ $b.push("x") }), " ", E({ $b.push(300).list.tail }), " ", E({ $b.append(1..2).elems }), " ", E({ $b.append([3,4]).elems }), " ", E({ $b.append(Blob.new(5)).elems }), " ", E({ $b.append(6, 7).elems }), " ", E({ $b.append("x") }), " ", $b.elems, " ", E({ $b.append(1, "x") }), " ", $b.elems, " ", E({ $b.append(1..*) }), " ", E({ $b.push(1..*) }), " ", E({ $b.pop }), " ", E({ $b.shift }), " ", E({ Buf.new.pop }), " ", E({ Buf.new.shift }), " ", do { try Buf.new.pop; my $e = $!; ((try $e.action) // "-") ~ "/" ~ ((try $e.what) // "-") }, " ", E({ $b.unshift(0).list.head }), " ", E({ $b.unshift(Blob.new(9,9)).list.head(2).raku }), " ", E({ $b.unshift(1, 2).list.head(3).raku }), " ", E({ $b.unshift((5, 5)) }), " ", E({ $b.prepend((5, 5)).list.head(2).raku }), " ", E({ $b.prepend(1..2).list.head(2).raku }), " ", E({ $b.prepend("x") }), " ", E({ $b.unshift("x") }), " ", E({ $b.push(4) === $b }), " ", $b.elems, " ", E({ $b.push(1, "x") }), " ", $b.elems, " ", E({ $b.append(array[int].new(1,2)).elems }), " ", E({ $b.prepend(array[int].new(1,2)).elems }), " ", E({ $b.push(1.5) }), " ", E({ $b.push(True).list.tail }), " ", E({ $b.append(1, 2**70).list.tail }), " ", do { try Buf.new.push("x"); my $e = $!; ((try $e.operation) // "-") ~ "/" ~ ((try $e.expected.^name) // "-") }, " ", E({ Buf.new.unshift(Buf.new(1)).raku }), " ", E({ $b.append(Buf.new).elems }), " ", E({ $b.append(()).elems }), " ", E({ $b.push().elems })
# rakudo 2026.08: ok:"Buf.new(1,2,3,4)" ok:6 ok:7 F:X::TypeCheck F:X::TypeCheck F:X::TypeCheck ok:9 F:X::TypeCheck ok:44 ok:12 ok:14 ok:15 ok:17 F:X::TypeCheck 17 T:X::TypeCheck 19 F:X::Cannot::Lazy F:X::TypeCheck ok:0 ok:1 F:X::Cannot::Empty F:X::Cannot::Empty pop/Buf ok:0 ok:"(9, 9).Seq" ok:"(1, 2, 9).Seq" F:X::TypeCheck ok:"(5, 5).Seq" ok:"(1, 2).Seq" F:X::TypeCheck F:X::TypeCheck ok:Bool::True 27 T:X::TypeCheck 29 ok:31 ok:33 F:X::TypeCheck ok:1 T:X::AdHoc push to Buf/uint8 ok:"Buf.new(1)" ok:36 ok:36 ok:36
```
rakupp 4.0.1-84: differs — a single list, Array or Range argument to `push` is flattened, a Str element is stored as 0 (no Failure, no throw), and `append(1..*)` reified a large prefix and filled the output, so the rest of the line (`pop`/`shift`, the empty-Buf Failures, the multi-value growth) is unmeasured.

### BB-12  splice                                                   D:partial R:partial V:bug
`splice(off, n)` removes n elements and returns them as a Buf of the same
type; `n` may be `*` (to the end), a Rat (truncated), or absent (to the
end); `splice(off, n, X)` inserts an Int, a Blob, a native int array or a
list of Ints in their place; a non-Int replacement is a Failure
`X::TypeCheck` and a bad element inside a list throws it, both leaving the
buffer intact. `splice()` with no arguments returns the whole buffer and
leaves the variable holding a new empty Buf; on a Buf that is not in a
container it empties that Buf in place and returns it, now empty. An
offset past the end pads the buffer with zeros up to the offset (`splice(20,
0, 5)` on 8 elements gives 21). The bugs: an offset past the end with a
positive count fails with `X::OutOfRange` AND STILL GROWS the buffer with
zeros to that offset; `splice(-1, 1)` and `splice(0, -1)` fail with
`X::OutOfRange` and leave a spurious leading 0; an infinite list as the
replacement never returns.
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; my $b = Buf.new(1,2,3,4,5); say E({ $b.splice(1, 2).raku }), " ", $b.raku, " ", E({ $b.splice(1, 1, 9).raku }), " ", $b.raku, " ", E({ $b.splice(0, 0, 7).raku }), " ", $b.raku, " ", E({ $b.splice(1, 2, Buf.new(8, 8, 8)).raku }), " ", $b.raku, " ", E({ $b.splice(1, 1, (6, 6)).raku }), " ", $b.raku, " ", E({ $b.splice(0, 1, [5]).raku }), " ", $b.raku, " ", E({ $b.splice(1, 1, "x") }), " ", $b.raku, " ", E({ $b.splice(1, 1, (1, "x")) }), " ", $b.raku, " ", E({ $b.splice(2).raku }), " ", $b.raku, " ", E({ $b.splice(0, *).raku }), " ", $b.raku, " ", E({ $b.push(1,2,3); $b.splice.raku }), " ", $b.raku, " ", E({ $b.push(1,2,3); $b.splice(9, 1) }), " ", $b.raku, " ", E({ $b.splice(1, 0).raku }), " ", E({ $b.splice(1, 1.9).raku }), " ", $b.raku, " ", E({ $b.splice(1, 1, 300); $b.raku }), " ", E({ $b.splice(0, 1, array[int].new(4, 4)).raku }), " ", $b.raku, " ", E({ $b.splice(1, 5).raku }), " ", $b.raku, " ", E({ Buf.new(1).splice.raku }), " ", E({ $b.splice(1, 2).^name }), " ", E({ $b.splice(0, 0, Blob.new(1, 2)).raku }), " ", $b.raku, " ", E({ $b.splice(-1, 1) }), " ", E({ $b.splice(0, -1) }), " ", E({ $b.splice(5, 0, 9).raku }), " ", $b.raku, " ", E({ $b.splice(3, 0, 1..2); $b.raku }), " ", E({ $b.splice("1", 1).raku }), " ", E({ $b.splice(9, 1) }), " ", $b.raku, " ", E({ $b.splice(20, 0, 5).raku }), " ", $b.raku
# rakudo 2026.08: ok:"Buf.new(2,3)" Buf.new(1,4,5) ok:"Buf.new(4)" Buf.new(1,9,5) ok:"Buf.new()" Buf.new(7,1,9,5) ok:"Buf.new(1,9)" Buf.new(7,8,8,8,5) ok:"Buf.new(8)" Buf.new(7,6,6,8,8,5) ok:"Buf.new(7)" Buf.new(5,6,6,8,8,5) F:X::TypeCheck Buf.new(5,6,6,8,8,5) T:X::TypeCheck Buf.new(5,6,6,8,8,5) ok:"Buf.new(6,8,8,5)" Buf.new(5,6) ok:"Buf.new(5,6)" Buf.new() ok:"Buf.new(1,2,3)" Buf.new() F:X::OutOfRange Buf.new(1,2,3,0,0,0,0,0,0) ok:"Buf.new()" ok:"Buf.new(2)" Buf.new(1,3,0,0,0,0,0,0) ok:"Buf.new(1,44,0,0,0,0,0,0)" ok:"Buf.new(1)" Buf.new(4,4,44,0,0,0,0,0,0) ok:"Buf.new(4,44,0,0,0)" Buf.new(4,0,0,0) ok:"Buf.new()" ok:"Buf" ok:"Buf.new()" Buf.new(1,2,4,0) F:X::OutOfRange F:X::OutOfRange ok:"Buf.new()" Buf.new(0,1,2,4,0,9) ok:"Buf.new(0,1,2,1,2,4,0,9)" T:X::Multi::NoMatch F:X::OutOfRange Buf.new(0,1,2,1,2,4,0,9,0) ok:"Buf.new()" Buf.new(0,1,2,1,2,4,0,9,0,0,0,0,0,0,0,0,0,0,0,0,5)
my $b = Buf.new(1,2,3); say $b.splice(0, 2, 1..*).raku
# rakudo 2026.08: (no output: killed by the 10-second alarm)
```
rakupp 4.0.1-84: differs — a Str replacement is stored as 0, `splice(9, 1)` past the end is an empty Buf with no growth (right), `splice(-1, 1)` removes the LAST element, `splice(0, -1)` removes nothing, a Str offset is accepted, `Buf.new(1).splice` does not empty the buffer, and the infinite replacement returns.

### BB-13  Typed variables and `is buf8`                            D:no R:yes V:spec
`my @a is buf8 = ^5` makes the variable a `Buf[uint8]`; assignment
re-stores (values wrap), `push` wraps, and an `is blob8` variable takes
its first assignment only (`X::Assignment::RO` after). A lazy list is
`X::Cannot::Lazy`, a non-Int element `X::TypeCheck`, a Blob on the right
is copied element-wise. Binding a plain `Buf` to an `is buf8` variable is
`X::TypeCheck::Binding`. A `my Buf $b` holds any Buf, a `my Blob $b` any
Blob including a Buf or a utf8, but a `my buf8 $b` does NOT take a plain
`Buf.new(1)` and a `my utf8 $b` does not take `"a".encode("ascii")`
(`X::TypeCheck::Assignment`); an Array is never coerced.
```
say do { my @a is buf8 = ^5; @a.raku }, " ", do { my @a is buf8 = ^5; @a = 7, 8; @a.raku }, " ", do { my @a is buf8; @a.push(300); @a.raku }, " ", do { my @a is Buf = 1, 2; @a ~~ Buf }, " ", do { my @a is blob8 = 1, 2; @a.raku }, " ", (try do { my @a is blob8 = 1, 2; @a = 3; @a.raku }) // $!.^name, " ", (try do { my @a is buf8 = 1..*; @a.elems }) // $!.^name, " ", do { my Buf $b .= new(1, 2); $b.raku }, " ", do { my Blob $b = "hi".encode; $b.^name }, " ", (try do { my Blob $b = [1, 2]; $b.raku }) // $!.^name, " ", do { my buf8 $b = buf8.new(1); $b.^name }, " ", (try do { my buf8 $b = Buf.new(1); $b.^name }) // $!.^name, " ", (try do { my Buf $b = buf8.new(1); $b.^name }) // $!.^name, " ", (try do { my blob8 $b = "a".encode; $b.^name }) // $!.^name, " ", do { my @a is buf8 = 1, 2; @a[0] = 9; @a[0] }, " ", do { my @a is buf16 = 70000; @a.list.raku }, " ", do { my @a is buf8 = Blob.new(1, 2); @a.raku }, " ", (try do { my @a is buf8 = 1, "x"; @a.raku }) // $!.^name, " ", do { my @a is buf8 = 1, 2; @a.^name }, " ", do { my @a is buf8 = 1,2; (@a.elems, @a[1]).raku }, " ", do { my @a is Blob = 1; @a.^name }, " ", (try do { my @a is Blob = 1; @a.push(2); @a.raku }) // $!.^name, " ", (try do { my @a is buf8 = 1; @a := Buf.new(5); @a.raku }) // $!.^name, " ", (try do { my Blob $b = Buf.new(1); $b.^name }) // $!.^name, " ", (try do { my utf8 $b = "a".encode; $b.^name }) // $!.^name, " ", (try do { my utf8 $b = "a".encode("ascii"); $b.^name }) // $!.^name, " ", (try do { my blob8 $b = blob16.new(1); $b.^name }) // $!.^name, " ", (try do { my @a is buf8 = ^3; @a.STORE((9,)); @a.raku }) // $!.^name, " ", do { my Buf $b = Buf.new(1); $b = Buf.new(2); $b.raku }, " ", do { my Buf $b .= new; $b.raku }
# rakudo 2026.08: Buf[uint8].new(0,1,2,3,4) Buf[uint8].new(7,8) Buf[uint8].new(44) True Blob[uint8].new(1,2) X::Assignment::RO X::Cannot::Lazy Buf.new(1,2) utf8 X::TypeCheck::Assignment Buf[uint8] X::TypeCheck::Assignment Buf[uint8] utf8 9 (4464,) Buf[uint8].new(1,2) X::TypeCheck Buf[uint8] (2, 2) Blob X::Multi::NoMatch X::TypeCheck::Binding Buf utf8 X::TypeCheck::Assignment X::TypeCheck::Assignment Buf[uint8].new(9) Buf.new(2) Buf.new()
```
rakupp 4.0.1-84: differs — the `is buf8`/`is blob8` traits are ignored (the variable is an Array, `@a ~~ Buf` is False, no wrapping, no RO), typed scalars accept every value, and `.STORE` is missing.

## D. Operators

### BB-14  Concatenation with `~`                                     D:partial R:yes V:spec
`~` between two buffers of the SAME type returns that type (`Blob`,
`Blob[uint8]`, `utf8`, `Blob[int8]`); between different types it returns
a plain `Buf` of 8-bit unsigned elements, re-narrowing the values (an
`int8` -1 becomes 255, a `uint16` 300 becomes 44). The result is a fresh
buffer (`===` to an equal Blob, the operands untouched). A `utf8`,
`utf16` or `utf32` with a Str on either side gives a Str (the buffer is
decoded); any other buffer with a Str, an Int, a List or `~$blob` throws
`X::Buf::AsStr`. `Any ~ $utf8` is the decoded text (with the usual
uninitialized-value warning). `[~]` of one buffer is that buffer, of
none is `""`; `~=` works.
```
say (try (Blob.new(1) ~ Blob.new(2)).raku) // $!.^name, " ", (try (blob8.new(1) ~ blob8.new(2)).raku) // $!.^name, " ", (try (Blob.new(1) ~ blob8.new(2)).raku) // $!.^name, " ", (try (Buf.new(1) ~ Blob.new(2)).raku) // $!.^name, " ", (try (utf8.new(97) ~ utf8.new(98)).raku) // $!.^name, " ", (try (utf8.new(97) ~ blob8.new(98)).raku) // $!.^name, " ", (try (Blob[int8].new(-1) ~ blob16.new(300)).raku) // $!.^name, " ", (try (utf8.new(97) ~ "b").raku) // $!.^name, " ", (try ("b" ~ utf8.new(97)).raku) // $!.^name, " ", (try Blob.new(97) ~ "b") // $!.^name, " ", (try "b" ~ Blob.new(97)) // $!.^name, " ", (try Buf.new(1) ~ 2) // $!.^name, " ", (try (quietly Any ~ "bar".encode).raku) // $!.^name, " ", (try ([~] Blob.new(1)).raku) // $!.^name, " ", (try ([~] Blob.new(1), Blob.new(2), Buf.new(3)).raku) // $!.^name, " ", (try do { my $a = Blob.new(1); $a ~= Blob.new(2); $a.raku }) // $!.^name, " ", (try ~Blob.new(1)) // $!.^name, " ", (try (~utf8.new(97)).raku) // $!.^name, " ", (try utf16.new(97) ~ "b") // $!.^name, " ", (try (Blob.new(1) ~ Blob.new(2)).^name) // $!.^name, " ", (try (Blob.new(1) ~ Buf.new(2)).^name) // $!.^name, " ", (try (Blob.new(1) ~ Blob.new).raku) // $!.^name, " ", (try (utf8.new(97) ~ utf8.new(98)).Str) // $!.^name, " ", (try (Blob.new(97) ~ utf8.new(98)).raku) // $!.^name, " ", (try (Blob[int8].new(-1) ~ Blob[int8].new(-2)).raku) // $!.^name, " ", (try (blob16.new(300) ~ blob8.new(1)).raku) // $!.^name, " ", (try (blob8.new(1) ~ blob16.new(300)).raku) // $!.^name, " ", (try Blob.new(1) ~ (1, 2)) // $!.^name, " ", ([~] ()).raku, " ", (try (Buf.new(1) ~ Buf.new(2)).^name) // $!.^name, " ", (try (buf8.new(1) ~ buf8.new(2)).^name) // $!.^name, " ", (try (Blob.new(1) ~ Blob.new(2)) === Blob.new(1, 2)) // $!.^name, " ", (try do { my $x = Buf.new(1); my $y = $x ~ Buf.new(2); $x.raku }) // $!.^name, " ", (try ("a".encode ~ "b".encode).^name) // $!.^name, " ", (try (utf8.new(97) ~ utf16.new(98)).raku) // $!.^name
# rakudo 2026.08: Blob.new(1,2) Blob[uint8].new(1,2) Buf.new(1,2) Buf.new(1,2) utf8.new(97,98) Buf.new(97,98) Buf.new(255,44) "ab" "ba" X::Buf::AsStr X::Buf::AsStr X::Buf::AsStr "bar" Blob.new(1) Buf.new(1,2,3) Blob.new(1,2) X::Buf::AsStr "a" ab Blob Buf Blob.new(1) ab Buf.new(97,98) Blob[int8].new(-1,-2) Buf.new(44,1) Buf.new(1,44) X::Buf::AsStr "" Buf Buf[uint8] True Buf.new(1) utf8 Buf.new(97,98)
```
rakupp 4.0.1-84: differs — `Blob ~ Blob` is a `Buf`, a mixed-width `~` stringifies the bytes into a Str, a plain buffer concatenates with a Str or Int instead of throwing, `~$blob` is `""`, the result is not `===` to an equal Blob, `(utf8 ~ utf8).Str` throws, and `utf8 ~ utf16` is a Str.

### BB-15  x, xx, X, Z, hypers and reductions                        D:no R:no V:quirk
`$blob x $n` stringifies and so throws `X::Buf::AsStr` (a `utf8` repeats
its text); `xx` repeats the same object. `X` treats a buffer as ONE item
(`Blob.new(1,2) X 3` is `((Blob, 3),)`, `X~` throws), while `Z`, `Z+`
and the hypers iterate its elements (`Z 3` is `((1, 3),)`, `>>+>> 1`
gives a `Blob` of the sums, `-<<` a Blob of wrapped negations, `<<+>>`
between two buffers is `X::Multi::Ambiguous`). A reduction sees the
buffer as one operand: `[+] Blob.new(1,2,3)` is 3 (the count), `[max]`
and `[~]` are the buffer itself. `xx *` is lazy and `.elems` on it fails.
```
say (try Blob.new(1) x 2) // $!.^name, " ", (utf8.new(97) x 3), " ", (Blob.new(1) xx 2).elems, " ", (Blob.new(1) xx 2).raku, " ", (try Blob.new(1) x 0) // $!.^name, " ", (utf8.new(97,98) x 2).raku, " ", (try Buf.new(1) x 1) // $!.^name, " ", (Blob.new(1,2) X 3).raku, " ", (Blob.new(1,2) Z 3).raku, " ", ((1, 2) Z Blob.new(5, 6)).raku, " ", (Blob.new(1,2) xx 2)[0] === (Blob.new(1,2) xx 2)[1], " ", (try (Blob.new(1,2) X~ "a").raku) // $!.^name, " ", (try (Blob.new(1,2) Z+ (10, 20)).raku) // $!.^name, " ", (try (Blob.new(1,2) >>+>> 1).raku) // $!.^name, " ", (try (Blob.new(1,2) <<+>> Blob.new(3,4)).raku) // $!.^name, " ", (try (-<< Blob.new(1,2)).raku) // $!.^name, " ", (try [+] Blob.new(1,2,3)) // $!.^name, " ", (try [max] Blob.new(1,2,3)) // $!.^name, " ", (try [~] Blob.new(1,2,3)) // $!.^name, " ", (try (Blob.new(1,2) xx *).elems) // $!.^name, " ", (try (utf8.new(97) x -1).raku) // $!.^name, " ", (utf8.new(97) x 2).^name
# rakudo 2026.08: X::Buf::AsStr aaa 2 (Blob.new(1), Blob.new(1)).Seq X::Buf::AsStr "abab" X::Buf::AsStr ((Blob.new(1,2), 3),).Seq ((1, 3),).Seq ((1, 5), (2, 6)).Seq True X::Buf::AsStr (11, 22).Seq Blob.new(2,3) X::Multi::Ambiguous Blob.new(255,254) 3 Blob:0x<01 02 03> Blob:0x<01 02 03> X::Cannot::Lazy "" Str
```
rakupp 4.0.1-84: differs — `x` gives `""` instead of throwing, `X` iterates the elements, `>>+>>`/`<<+>>`/`-<<` numify the buffer to its count, and `[+]` is the buffer itself.

### BB-16  Bitwise `~&`, `~|`, `~^`, prefix `~^`                     D:no R:yes V:bug
Element-wise on the raw values; the result has the LEFT operand's type
and the longer length: `~&` pads the shorter operand with zeros, `~|`
and `~^` copy the longer operand's tail. Values are stored in the result
type's width (`blob8 ~& int8(-1)` reads 255, `int8 ~^ blob8(255)` reads
0). Prefix `~^` complements within the width (`~^Blob.new(0)` is 255,
`~^blob16.new(0)` 65535, on `int8` -1). With a Str or Int operand `~&`
throws `X::Buf::AsStr` (a `utf8` stringifies instead); `~<`/`~>` have no
buffer candidate (`X::Multi::NoMatch`); `+&`/`+|`/`+<` numify. THE BUG:
`~&` and `~|` between two SIGNED buffers of different lengths, and `~&`
with a signed left operand and an unsigned shorter/longer right one,
throw `X::AdHoc` (an internal error while padding), while `~^` and the
equal-length cases work; do not imitate.
```
say (Blob.new(0xF0, 0x0F) ~& Blob.new(0xFF, 0x00)).raku, " ", (Blob.new(0xF0) ~| Blob.new(0x0F, 0x11)).raku, " ", (Blob.new(0xF0, 0x0F) ~^ Blob.new(0xFF)).raku, " ", (Blob.new(1,1,1,1,1) ~& Blob.new(1,1)).raku, " ", (Blob.new(1,1,1,1,1) ~| Blob.new(2,2)).raku, " ", (Blob.new(1,1,1,1,1) ~^ Blob.new(1,1)).raku, " ", (Blob.new(1,1) ~| Blob.new(2,2,2,2)).raku, " ", (Blob.new(1,1) ~^ Blob.new(1,1,3,3)).raku, " ", (~^Blob.new(0, 1, 255)).raku, " ", (~^Blob[int8].new(0, 1, -1)).raku, " ", (~^blob16.new(0)).raku, " ", (utf8.new(1) ~& blob8.new(3)).raku, " ", (blob8.new(1) ~& utf8.new(3)).raku, " ", (Buf.new(1) ~| Blob.new(3)).raku, " ", (Blob.new(1) ~| Buf.new(3)).raku, " ", (Blob[int8].new(-1) ~& blob8.new(255)).raku, " ", (blob8.new(255) ~& Blob[int8].new(-1)).raku, " ", (Blob[int8].new(-1) ~| Blob[int8].new(0)).raku, " ", (blob8.new(255) ~^ Blob[int8].new(-1)).raku, " ", (Blob[int8].new(-1) ~^ blob8.new(255, 1)).raku, " ", ("foo".encode ~& "bar".encode).raku, " ", (~^"foo".encode).raku, " ", ("foo".encode ~^ "bar".encode).raku, " ", (try Blob.new(1) ~& "x") // $!.^name, " ", (try Blob.new(1) ~& 1) // $!.^name, " ", (Blob.new(1,2) +& Blob.new(1)), " ", (Blob.new(1,2) +^ 3), " ", (blob16.new(0x0F0F) ~& blob16.new(0xFFFF)).raku, " ", (blob16.new(1) ~& blob8.new(1)).^name, " ", (Blob.new ~& Blob.new(1)).raku, " ", (Blob.new(1) ~| Blob.new).raku, " ", (~^Blob.new).raku, " ", (try (Blob.new(1,2) ~< 1).raku) // $!.^name, " ", (try Blob.new(1,2) +< 1) // $!.^name, " ", (try (Blob[int8].new(-1) ~| blob8.new(0)).raku) // $!.^name, " ", (try (blob8.new(0) ~| Blob[int8].new(-1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1,-1) ~& Blob[int8].new(-1)).raku) // $!.^name, " ", (try utf8.new(97) ~& "a") // $!.^name, " ", (~^utf8.new(0)).^name
# rakudo 2026.08: Blob.new(240,0) Blob.new(255,17) Blob.new(15,15) Blob.new(1,1,0,0,0) Blob.new(3,3,1,1,1) Blob.new(0,0,1,1,1) Blob.new(3,3,2,2) Blob.new(0,0,3,3) Blob.new(255,254,0) Blob[int8].new(-1,-2,0) Blob[uint16].new(65535) utf8.new(1) Blob[uint8].new(1) Buf.new(3) Blob.new(3) Blob[int8].new(-1) Blob[uint8].new(255) Blob[int8].new(-1) Blob[uint8].new(0) Blob[int8].new(0,1) utf8.new(98,97,98) utf8.new(153,144,144) utf8.new(4,14,29) X::Buf::AsStr X::Buf::AsStr 0 1 Blob[uint16].new(3855) Blob[uint16] Blob.new(0) Blob.new(1) Blob.new() X::Multi::NoMatch 4 Blob[int8].new(-1) Blob[uint8].new(255) X::AdHoc a utf8
say (try (Blob[int8].new(-1,-1) ~& Blob[int8].new(-1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1) ~& Blob[int8].new(-1,-1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1,-1) ~| Blob[int8].new(1)).raku) // $!.^name, " ", (try (Blob[int8].new(1) ~| Blob[int8].new(-1,-1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1,-1) ~^ Blob[int8].new(1)).raku) // $!.^name, " ", (try (Blob[int8].new(1) ~^ Blob[int8].new(-1,-1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1,-1) ~& Blob[int8].new(-1,-1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1,-1) ~| Blob[int8].new(1,1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1,-1) ~& blob8.new(1)).raku) // $!.^name, " ", (try (blob8.new(1) ~& Blob[int8].new(-1,-1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1,-1) ~| blob8.new(1)).raku) // $!.^name, " ", (try (blob8.new(1) ~| Blob[int8].new(-1,-1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1,-1) ~^ blob8.new(1)).raku) // $!.^name, " ", (try (blob8.new(1) ~^ Blob[int8].new(-1,-1)).raku) // $!.^name, " ", (try (Blob[int16].new(-1,-1) ~& Blob[int16].new(-1)).raku) // $!.^name, " ", (try (Blob[int8].new(1,1) ~& Blob[int8].new(1)).raku) // $!.^name, " ", (try (Blob[int8].new(1,1) ~| Blob[int8].new(1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1,-1) ~| Blob[int8].new(-1)).raku) // $!.^name, " ", (try (Blob[int8].new(-1) ~| Blob[int8].new(-1,-1)).raku) // $!.^name, " ", (try (Blob[int8].new(1,1) ~^ Blob[int8].new(1)).raku) // $!.^name, " ", (try (Blob[int8].new(-2,-2) ~^ Blob[int8].new(1)).raku) // $!.^name
# rakudo 2026.08: X::AdHoc X::AdHoc X::AdHoc X::AdHoc Blob[int8].new(-2,-1) Blob[int8].new(-2,-1) Blob[int8].new(-1,-1) Blob[int8].new(-1,-1) X::AdHoc Blob[uint8].new(1,0) Blob[int8].new(-1,-1) Blob[uint8].new(255,255) Blob[int8].new(-2,-1) Blob[uint8].new(254,255) X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc Blob[int8].new(0,1) Blob[int8].new(-1,-2)
```
rakupp 4.0.1-84: differs — `~^int8(0,1,-1)` and `~^blob16(0)` are narrowed to 8 bits (`255,254,0`, `255,255`), `~^int8(0,1,-1)` also loses its type (a plain `Blob`), `blob16 ~& blob8` answers a `Blob`, `utf8 ~& "a"` throws `X::Buf::AsStr` where Rakudo stringifies, and `~^utf8` is a `Blob`; the unequal-length signed cases all COMPUTE, with zero padding for `~&` and tail copying for `~|` (`(-1,0)`, `(-1,-1)`, …), which is the behaviour to keep.

### BB-17  ACCEPTS and smartmatch                                    D:no R:partial V:spec
Two buffers smartmatch when they have the same element count and every
pair of elements is `==` — across types and widths, by value (`int8` -1
is not `blob8` 255). Against a type, a buffer is its role and its
parameterisation only (`~~ Blob` True for every buffer; `~~ blob8` False
for a plain Blob, True for a utf8; `~~ Buf` False for a Blob). A List or
Array on the right never matches (the buffer is one non-iterable item);
a number on the right compares the element count; a Str on the right
compares `Str.ACCEPTS`, False for a plain buffer (no throw) and text
equality for a `utf8`; a `Set` on the right compares the element sets;
a Regex on the right is tried against EACH ELEMENT's numeric text and
returns the first Match (`Blob.new(10,11) ~~ /.+/` is the Match `10`;
a `utf8` is not decoded for this). `~~ Positional`, `~~ Stringy`, `~~ Any`
are True; `~~ Iterable`, `~~ Int`, `~~ Cool` False.
```
say (Blob.new(1,2) ~~ Blob.new(1,2)), " ", (Blob.new(1,2) ~~ Blob.new(1,3)), " ", (Blob.new(1,2) ~~ Blob.new(1,2,0)), " ", (buf8.new(1) ~~ blob8.new(1)), " ", (Blob.new(1) ~~ Buf.new(1)), " ", (Blob[int8].new(-1) ~~ blob8.new(255)), " ", (Blob.new(1,2) ~~ (1,2)), " ", (Blob.new(1,2) ~~ (1,3)), " ", (Blob.new(1,2) ~~ [1,2]), " ", ((1,2) ~~ Blob.new(1,2)), " ", (Blob.new(1,2) ~~ Blob), " ", (Blob.new(1,2) ~~ blob8), " ", (Blob.new(1,2) ~~ Blob[uint8]), " ", (blob8.new(1) ~~ Blob), " ", (Buf.new(1) ~~ Blob), " ", (Blob.new(1) ~~ Buf), " ", (buf8.new(1) ~~ Buf), " ", (buf8.new(1) ~~ Buf[uint8]), " ", (utf8.new(97) ~~ blob8), " ", (utf8.new(97) ~~ utf8), " ", (blob8.new(97) ~~ utf8), " ", (utf8.new(97) ~~ "a"), " ", (try Blob.new(97) ~~ "a") // $!.^name, " ", (Blob.new(1,2) ~~ 2), " ", (Blob.new(1,2) ~~ Positional), " ", (Blob.new(1,2) ~~ Stringy), " ", (Blob.new(1,2) ~~ Iterable), " ", (Blob.new ~~ Blob.new), " ", (Blob.new(1,2) ~~ Blob.new(1,2)).^name, " ", ("a".encode ~~ blob8), " ", ("a".encode("ascii") ~~ blob8), " ", ("a".encode("ascii") ~~ utf8), " ", (Blob.new(1,2) ~~ Blob.new(1,2).list), " ", (Blob.new(1,2).list ~~ Blob.new(1,2)), " ", (Blob.new(1,2) ~~ 1), " ", (Blob.new(1,2) ~~ Blob.new(2,1)), " ", (Blob.new ~~ ()), " ", ?(Blob.new(1,2) ~~ /1/), " ", ?(Blob.new(1,2) ~~ /Blob/), " ", ?(Blob.new(0x0A) ~~ /"0A"/), " ", ?(utf8.new(97) ~~ /a/), " ", ?(utf8.new(97) ~~ /utf8/), " ", ?(utf8.new(97) ~~ /61/), " ", (Blob.new(1,2) ~~ /1/).^name, " ", ?(Blob.new(1,2) ~~ /12/), " ", ?(Blob.new(1,2) ~~ /^12$/), " ", (Blob.new(1,2) ~~ /.+/).Str, " ", (Blob.new(10,11) ~~ /.+/).Str, " ", (Blob.new(1) ~~ Cool), " ", (Blob.new(1,2) ~~ *.elems), " ", (Blob.new(1,2) ~~ Int), " ", (Blob.new(1,2) ~~ Any), " ", (Blob.new(1,2) ~~ (1, 2, 3)), " ", (Blob.new(1,2) ~~ Buf.new(1,2)), " ", (Buf.new(1,2) ~~ Blob.new(1,2)), " ", (try Blob.new(1,2) ~~ Blob.new(1,2).Buf) // $!.^name, " ", (Blob.new(1) ~~ blob16.new(1)), " ", (Blob.new(1,2) ~~ Set(1,2)), " ", (Blob.new(1,2) ~~ Blob.new(1,2).Set)
# rakudo 2026.08: True False False True True False False False False False True False False True True False True True True True False True False True True True False True Bool True True False False False False False False True False False False False False Match False False 1 10 False True False True False True True True True True True
```
rakupp 4.0.1-84: differs — an `int8` -1 matches a `blob8` 255, a plain Blob is a `blob8`, a `blob8` is a `utf8`, `Blob ~~ "a"` is True, `Blob.new ~~ ()` is True, a Regex on the right gives `Nil` for a plain buffer (no element scan) but matches a `utf8`'s decoded text, the two Set comparisons are False, and `.Buf` is missing.

## E. A buffer as a list

### BB-18  Iteration and the Positional methods                      D:partial R:partial V:quirk
`for` over a buffer value iterates its elements; over a scalar holding
one it runs once. `.list`/`.List` give a List of Ints, `.Array`, `.Seq`,
`.Slip`, `.flat`, `.map`, `.grep`, `.sort` (a Seq), `.first`, `.sum`,
`.min`, `.max`, `.join`, `.keys`, `.values`, `.kv`, `.pairs`,
`.antipairs`, `.head`, `.tail`, `.skip`, `.pick`, `.roll`, `.unique`,
`.squish`, `.minmax`, `.are`, `.cache`, `.batch`, `.rotor`,
`.combinations`, `.Set`, `.Bag`, `.Mix`, `.Capture`, `.end` all work on
the elements; `.reverse` returns a buffer of the same type, not a Seq.
`.rotate`, `.contains` and `.index` do not exist. `.reduce` and
`.produce` see the buffer as ONE item (`.reduce(&[+])` is 3, the count —
the quirk). `.is-lazy` is False.
```
my $b = Blob.new(3,1,2); say do { my $n = 0; for Blob.new(3,1,2) { $n++ }; $n }, " ", do { my $n = 0; for $b { $n++ }; $n }, " ", do { my $n = 0; for @$b { $n++ }; $n }, " ", do { my $n = 0; for $b.list { $n++ }; $n }, " ", $b.list.raku, " ", $b.List.raku, " ", $b.Array.raku, " ", $b.Seq.raku, " ", $b.Slip.raku, " ", $b.flat.raku, " ", $b.map(* + 1).raku, " ", $b.grep(* > 1).raku, " ", $b.sort.raku, " ", $b.sort.^name, " ", $b.first(* > 1), " ", $b.sum, " ", $b.min, " ", $b.max, " ", $b.join("-"), " ", $b.join.raku, " ", $b.keys.raku, " ", $b.values.raku, " ", $b.kv.raku, " ", $b.pairs.raku, " ", $b.head.raku, " ", $b.head(2).raku, " ", $b.tail.raku, " ", $b.pick(3).sort.raku, " ", $b.roll(2).elems, " ", $b.reverse.raku, " ", $b.reverse.^name, " ", (try $b.rotate.raku) // $!.^name, " ", $b.unique.raku, " ", $b.elems, " ", $b.end, " ", $b.antipairs.raku, " ", $b.Bag.elems, " ", (try $b.contains(1)) // $!.^name, " ", (try $b.index(1)) // $!.^name, " ", $b.iterator.^name, " ", $b.list.^name, " ", $b.List.^name, " ", (try $b.cache.raku) // $!.^name, " ", (try $b.is-lazy) // $!.^name, " ", (try $b.are.^name) // $!.^name, " ", (try $b.minmax.raku) // $!.^name, " ", (try $b.squish.raku) // $!.^name, " ", (try $b.reduce(&[+])) // $!.^name, " ", (try $b.produce(&[+]).raku) // $!.^name, " ", (try $b.combinations(2).elems) // $!.^name, " ", (try $b.batch(2).raku) // $!.^name, " ", (try $b.rotor(2).raku) // $!.^name, " ", $b.map(*.^name).raku, " ", $b.keys.^name, " ", $b.Array.^name, " ", $b.first(* > 5).raku, " ", $b.grep(* > 1).^name, " ", $b.sum.^name, " ", $b.pairs.head.raku, " ", $b.Array[0].VAR.^name, " ", $b.Slip.^name, " ", $b.Set.elems, " ", $b.Mix.^name, " ", ($b.list ~~ List), " ", $b.list.is-lazy, " ", $b.Capture.^name, " ", $b.elems.^name, " ", $b.kv.^name, " ", $b.head(0).raku, " ", $b.tail(0).raku, " ", $b.tail(2).raku, " ", (try $b.skip.raku) // $!.^name, " ", (try $b.max(*.Str).raku) // $!.^name, " ", $b.sort(-*).raku, " ", $b.sort.reverse.raku, " ", $b.map({ $_ }).sum, " ", $b.max.^name
# rakudo 2026.08: 3 1 3 3 (3, 1, 2) (3, 1, 2) [3, 1, 2] (3, 1, 2).Seq slip(3, 1, 2) (3, 1, 2).Seq (4, 2, 3).Seq (3, 2).Seq (1, 2, 3).Seq Seq 3 6 1 3 3-1-2 "312" (0, 1, 2).Seq (3, 1, 2) (0, 3, 1, 1, 2, 2).Seq (0 => 3, 1 => 1, 2 => 2).Seq 3 (3, 1).Seq 2 (1, 2, 3).Seq 2 Blob.new(2,1,3) Blob X::Method::NotFound (3, 1, 2).Seq 3 2 (3 => 0, 1 => 1, 2 => 2).Seq 3 X::Method::NotFound X::Method::NotFound Rakudo::Iterator::ReifiedListIterator List List (3, 1, 2) False Int 1..3 (3, 1, 2).Seq 3 (Blob.new(3,1,2),).Seq 3 ((3, 1), (2,)).Seq ((3, 1),).Seq ("Int", "Int", "Int").Seq Seq Array Nil Seq Int 0 => 3 Scalar Slip 3 Mix True False Capture Int Seq ().Seq ().Seq (1, 2).Seq (1, 2).Seq 3 (3, 2, 1).Seq (3, 2, 1).Seq 6 Int
```
rakupp 4.0.1-84: differs — `for $scalar` iterates the elements, `.sum` is the count, `.min`/`.max` are the buffer, `.join` is empty, `.reverse` is a Seq, `.contains`/`.index` answer, `.is-lazy` and `.^roles` are missing, `.reduce` does add (6), `.are` is `Blob`, `.minmax` is `3..3`, `.head(0)`/`.tail(0)` return an element, and `.max(*.Str)` returns the buffer.

### BB-19  A Buf hands out live references                          D:no R:no V:quirk
Every element view of a Buf — `.list`, `.List`, `.values`, `.sort`,
`.grep`, `.head`, `.pairs[i].value`, `.Seq`, `for @$buf`, `for
$buf.list`, `for .kv -> $k, $v is rw`, `.map({ $_ = 5 })`, `AT-POS` —
is a reference into the buffer: assigning to it changes the buffer
(wrapping to the width; a Str or Rat throws `X::AdHoc`), and a bound
reference survives a later `push`. `.Array`, `my @l = $buf.list`,
`.reverse`, `.subbuf` and `Buf.new($buf)` are copies. A Blob's element
views are plain values (`.VAR` is `Int`; assignment is refused). Two
scalars assigned the same Buf share it.
```
my $b = Buf.new(3,1,2); say (try do { for $b.list { $_ = 9 }; $b.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); for $c { $_++ }; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); for @$c { $_++ }; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.list[0] = 7; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); my @l = $c.list; @l[0] = 7; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.map({ $_ = 5 }); $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.map({ $_ = 5 }).eager; $c.raku }) // $!.^name, " ", (try do { my $c = Blob.new(1,2); for $c.list { $_ = 9 }; $c.raku }) // $!.^name, " ", (try do { my $c = Blob.new(1,2); for $c { $_++ }; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.list[0] = 300; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.List[1] = 0; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.Array[0] = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.sort[0] = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.values[0] = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.reverse[0] = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.head = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.grep(* > 0)[0] = 9; $c.raku }) // $!.^name, " ", do { my $c = Buf.new(1,2); $c.list[0].VAR.^name }, " ", do { my $c = Blob.new(1,2); $c.list[0].VAR.^name }, " ", (try do { my $c = Buf.new(1,2); $c.Seq[0] = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.pairs[0].value = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); for $c.kv -> $k, $v is rw { $v = 8 }; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.list[0] = "x"; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.list[0] = 1.5; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); my $r := $c.list[0]; $c.push(3); $r = 4; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); $c.subbuf(0)[0] = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); my $d = Buf.new($c); $d[0] = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); my $d = $c; $d[0] = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); my @a = $c; @a[0] = 9; $c.raku }) // $!.^name, " ", (try do { my $c = Buf.new(1,2); my @a = @$c; @a[0] = 9; $c.raku }) // $!.^name
# rakudo 2026.08: Buf.new(9,9,9) X::Method::NotFound Buf.new(2,3) Buf.new(7,2) Buf.new(1,2) Buf.new(5,5) Buf.new(5,5) X::AdHoc X::Method::NotFound Buf.new(44,2) Buf.new(1,0) Buf.new(1,2) Buf.new(9,2) Buf.new(9,2) Buf.new(1,2) Buf.new(9,2) Buf.new(9,2) UIntPosRef Int Buf.new(9,2) Buf.new(9,2) Buf.new(8,8) X::AdHoc X::AdHoc Buf.new(4,2,3) Buf.new(1,2) Buf.new(1,2) Buf.new(9,2) Buf.new(1,2) Buf.new(1,2)
```
rakupp 4.0.1-84: differs — no view is a reference: assignment through `.list`, `.sort`, `.head`, `.values`, `.grep`, `.pairs`, `.Seq`, `AT-POS` or `.Array` is `X::Assignment::RO`, `for $buf.list { $_ = 9 }` and `.map({ $_ = 5 })` do nothing, and `my $d = $c` COPIES the buffer where Rakudo shares it.

### BB-20  AT-POS, EXISTS-POS and the adverbs                        D:no R:partial V:spec
A Blob index past the end or negative is a Failure `X::OutOfRange`
(`.what` "Index", `.got` the index, `.range` `0..elems-1`); a Buf reads
0 past the end (no growth) and fails only for a negative index. A slice
past the end THROWS `X::OutOfRange` on a Blob and reads zeros on a Buf; a
negative index inside a Range slice throws. `Inf` throws
`X::Numeric::CannotConvert`, a non-numeric Str `X::Str::Numeric`, `Nil`
`X::AdHoc`; a Rat or Num index truncates, a numeric Str converts.
`EXISTS-POS` is False past the end, for a negative index and for a
fractional one. `:v`, `:p`, `:kv` work, an out-of-range `:p` is `()`;
`[]` (zen) is the buffer itself, `[*]` the elements. `$buf.AT-POS(i) =
v` writes.
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; my $i = -1; my $b = Blob.new(5,6); my $c = Buf.new(5,6); say E({ $b[2] }), " ", E({ $b[$i] }), " ", E({ $c[2] }), " ", $c.elems, " ", E({ $c[$i] }), " ", E({ $b[1.9] }), " ", E({ $c["1"] }), " ", E({ $b[*-1] }), " ", E({ $c[*-3] }), " ", E({ $b[0, 5].map({ $_ ~~ Failure ?? "F" !! $_ }).raku }), " ", E({ $c[0, 5].raku }), " ", ($b[1]:exists), " ", ($b[2]:exists), " ", ($b[$i]:exists), " ", ($c[9]:exists), " ", E({ $b[1]:v }), " ", E({ $b[1]:p }), " ", E({ $b[1]:kv }), " ", E({ $b[0..*].raku }), " ", E({ $b[^2].raku }), " ", E({ $c[1..3].raku }), " ", do { my $e = Blob.new(1)[5]; $e.so; ((try $e.exception.what) // "-") ~ "/" ~ ((try $e.exception.got) // "-") ~ "/" ~ ((try $e.exception.range) // "-") }, " ", E({ $b[0][0] }), " ", E({ $b.AT-POS(1) }), " ", E({ $b.EXISTS-POS(1) }), " ", E({ $c.AT-POS(1) = 7; $c.raku }), " ", E({ $b[Inf] }), " ", E({ $b[].raku }), " ", E({ $b[*].raku }), " ", E({ $b[$i]:p }), " ", E({ $b[5]:exists }), " ", E({ $b.EXISTS-POS(-1) }), " ", E({ $b.EXISTS-POS(1.5) }), " ", E({ $b[1 .. 1].raku }), " ", E({ $b[2 .. 1].raku }), " ", E({ $b[$i .. 1].raku }), " ", E({ $c[2 .. 3].raku }), " ", $c.elems, " ", E({ $c[3, 2].raku }), " ", $c.elems, " ", E({ $b[1e0] }), " ", E({ $b["x"] }), " ", E({ $b[Nil] }), " ", do { my $e = Blob.new(1)[$i]; $e.so; ((try $e.exception.what) // "-") ~ "/" ~ ((try $e.exception.got) // "-") ~ "/" ~ ((try $e.exception.range) // "-") }, " ", E({ $b[2].^name }), " ", E({ $b[0, 5].^name })
# rakudo 2026.08: F:X::OutOfRange F:X::OutOfRange ok:0 2 F:X::OutOfRange ok:6 ok:6 ok:6 F:X::OutOfRange T:X::OutOfRange ok:"(5, 0)" True False False False ok:6 ok:1 => 6 ok:(1, 6) ok:"(5, 6)" ok:"(5, 6)" ok:"(6, 0, 0)" Index/5/0..0 ok:5 ok:6 ok:Bool::True ok:"Buf.new(5,7)" T:X::Numeric::CannotConvert ok:"Blob.new(5,6)" ok:"(5, 6)" ok:() ok:Bool::False ok:Bool::False ok:Bool::False ok:"(6,)" ok:"()" T:X::OutOfRange ok:"(0, 0)" 2 ok:"(0, 0)" 2 ok:6 T:X::Str::Numeric T:X::AdHoc Index/-1/0..0 ok:"Failure" T:X::OutOfRange
```
rakupp 4.0.1-84: differs — out-of-range reads are `Any` on both Blob and Buf (no Failure, no zeros), `:exists` is True for a negative index, the adverbs return `()`, `:p` on a negative index is a Pair holding the whole buffer, slices truncate, `Inf` is `Any`, `"x"`/`Nil` indices give 5, the Failure has no `.what`/`.got`/`.range`, and `AT-POS(i) = v` is `X::Method::NotFound`.

### BB-21  subbuf                                                    D:yes R:yes V:bug
`subbuf(from)`, `subbuf(from, len)`, `subbuf(range)` (integer bounds,
`^n`, `a..^b`, `a^..b`; an empty or reversed range is empty; a range
past the end is clipped), `subbuf(*-n)`, `subbuf(from, *-n)`,
`subbuf(*-n, *-m)`, `subbuf(from, *)`, `subbuf(from, Inf)`, `subbuf(from,
3.3)` (a Numeric length truncates, `Inf` means to the end). The result
has the invocant's type. A negative or past-the-end start is a Failure
`X::OutOfRange` (`.what` `"From argument to subbuf` — with that stray
quote —, `.got` the start, `.range` `0..elems`); a negative length a
Failure `X::OutOfRange` (`.what` "Len element to subbuf"); a length that
runs past the end is clipped; `subbuf(elems)` is empty; a Range with a
non-integer or infinite bound is a Failure `X::AdHoc`. A Str start or a
Str length, or a Rat start, is `X::Multi::NoMatch`. Two equal Blob
subbufs are `===`. THE BUG: a Str start WITH an Int length
(`subbuf("1", 1)`) recurses forever.
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; my $b = Blob.new(^10); say E({ $b.subbuf(7).raku }), " ", E({ $b.subbuf(2, 3).raku }), " ", E({ $b.subbuf(2..4).raku }), " ", E({ $b.subbuf(2..^4).raku }), " ", E({ $b.subbuf(2^..4).raku }), " ", E({ $b.subbuf(^3).raku }), " ", E({ $b.subbuf(*-3).raku }), " ", E({ $b.subbuf(*-3, 2).raku }), " ", E({ $b.subbuf(2, *-6).raku }), " ", E({ $b.subbuf(*-4, *-2).raku }), " ", E({ $b.subbuf(8, *).raku }), " ", E({ $b.subbuf(8, Inf).raku }), " ", E({ $b.subbuf(8, 5).raku }), " ", E({ $b.subbuf(8, 1.9).raku }), " ", E({ $b.subbuf(10).raku }), " ", E({ $b.subbuf(11) }), " ", E({ $b.subbuf(-1) }), " ", E({ $b.subbuf(*-11) }), " ", E({ $b.subbuf(0, -1) }), " ", E({ $b.subbuf(5..1).raku }), " ", E({ $b.subbuf(5..5).raku }), " ", E({ $b.subbuf(5..50).raku }), " ", E({ $b.subbuf(1.5..3) }), " ", E({ $b.subbuf(1..*).raku }), " ", E({ $b.subbuf(0, 0).raku }), " ", E({ Blob.new.subbuf(0, 1).raku }), " ", E({ Blob.new.subbuf(1) }), " ", $b.subbuf(1).^name, " ", "abc".encode.subbuf(1).^name, " ", buf8.new(1,2).subbuf(1).^name, " ", Buf.new(1,2).subbuf(1).^name, " ", Blob[int8].new(-1, 2).subbuf(1).^name, " ", do { my $e = $b.subbuf(-1); $e.so; ((try $e.exception.what) // "-") ~ "/" ~ ((try $e.exception.got) // "-") ~ "/" ~ ((try $e.exception.range) // "-") }, " ", do { my $e = $b.subbuf(0, -2); $e.so; ((try $e.exception.what) // "-") ~ "/" ~ ((try $e.exception.got) // "-") ~ "/" ~ ((try $e.exception.range) // "-") }, " ", E({ $b.subbuf("2").raku }), " ", E({ $b.subbuf(2, "3").raku }), " ", E({ $b.subbuf(2.5).raku }), " ", E({ $b.subbuf(*-1, *).raku }), " ", E({ $b.subbuf(0, *-11) }), " ", E({ $b.subbuf(10, 5).raku }), " ", E({ $b.subbuf(-1..3) }), " ", E({ $b.subbuf(8..20).raku }), " ", E({ $b.subbuf(0, 3e0).raku }), " ", E({ $b.subbuf(0, 2.5).raku }), " ", E({ $b.subbuf(2..*-1).raku }), " ", E({ $b.subbuf(2, -0).raku }), " ", E({ Blob.subbuf(0) }), " ", E({ $b.subbuf(3).subbuf(3).raku }), " ", E({ $b.subbuf(0, 3) === $b.subbuf(0, 3) }), " ", E({ Buf.new(1,2).subbuf(0) === Buf.new(1,2).subbuf(0) })
# rakudo 2026.08: ok:"Blob.new(7,8,9)" ok:"Blob.new(2,3,4)" ok:"Blob.new(2,3,4)" ok:"Blob.new(2,3)" ok:"Blob.new(3,4)" ok:"Blob.new(0,1,2)" ok:"Blob.new(7,8,9)" ok:"Blob.new(7,8)" ok:"Blob.new(2,3,4)" ok:"Blob.new(6,7,8)" ok:"Blob.new(8,9)" ok:"Blob.new(8,9)" ok:"Blob.new(8,9)" ok:"Blob.new(8)" ok:"Blob.new()" F:X::OutOfRange F:X::OutOfRange F:X::OutOfRange F:X::OutOfRange ok:"Blob.new()" ok:"Blob.new(5)" ok:"Blob.new(5,6,7,8,9)" F:X::AdHoc ok:"Failure.new(exception => X::AdHoc.new(payload => \"Must specify a Range with integer bounds to subbuf\"), backtrace => Backtrace.new)" ok:"Blob.new()" ok:"Blob.new()" F:X::OutOfRange Blob utf8 Buf[uint8] Buf Blob[int8] "From argument to subbuf/-1/0..10 Len element to subbuf/-2/0..10 T:X::Multi::NoMatch T:X::Multi::NoMatch T:X::Multi::NoMatch ok:"Blob.new(9)" ok:Blob.new() ok:"Blob.new()" F:X::OutOfRange ok:"Blob.new(8,9)" ok:"Blob.new(0,1,2)" ok:"Blob.new(0,1)" T:X::AdHoc ok:"Blob.new()" T:X::Multi::NoMatch ok:"Blob.new(6,7,8,9)" ok:Bool::True ok:Bool::False
say Blob.new(1,2).subbuf("1", 1).raku
# rakudo 2026.08: (no output: killed by the 10-second alarm)
```
rakupp 4.0.1-84: differs — a negative, past-the-end or Str/Rat start and a negative length all answer a buffer (from the end, empty or the whole), a non-integer Range is accepted, a `utf8`/`Blob[int8]` subbuf is a plain `Blob`, the Failures carry no attributes, `Blob.subbuf(0)` is `X::Method::NotFound`, and the Str-start case returns `Blob.new(2)`.

### BB-22  subbuf-rw                                                 D:yes R:yes V:quirk
`subbuf-rw($from = 0, $elems = elems - $from)` returns a Proxy: assigning
a Blob (any Blob, values re-narrowed) replaces that stretch, growing or
shrinking the buffer; `(n, 0)` inserts, `= Buf.new` deletes. The sub
form `subbuf-rw($buf, $from?, $elems?)` does the same. Assigning a
non-Blob (a list, an Int, a Str) is `X::TypeCheck::Binding::Parameter`;
a start past the end, a negative start or a negative count is
`X::TypeCheck::Assignment` (the failed subbuf cannot be stored). The
quirk: reading the Proxy after assigning through it still gives the
ORIGINAL stretch, not the new one. A Blob has no `subbuf-rw`
(`X::Method::NotFound`; the sub form is `X::Multi::NoMatch`).
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; my $b = Buf.new(0..5); say E({ $b.subbuf-rw(3, 1) = Buf.new(100, 101); $b.raku }), " ", E({ $b.subbuf-rw(3) = Buf.new(200); $b.raku }), " ", E({ $b.subbuf-rw = Buf.new(7, 7); $b.raku }), " ", E({ $b.subbuf-rw(0, 0) = Buf.new(1); $b.raku }), " ", E({ $b.subbuf-rw(2, 0) = Blob.new(9); $b.raku }), " ", E({ $b.subbuf-rw(1, 2) = Buf.new; $b.raku }), " ", E({ $b.subbuf-rw(1, 1).raku }), " ", E({ $b.subbuf-rw(1, 1).^name }), " ", E({ subbuf-rw($b, 1, 1) = Buf.new(5, 5); $b.raku }), " ", E({ subbuf-rw($b, 1) = Buf.new(3); $b.raku }), " ", E({ subbuf-rw($b) = Buf.new(4); $b.raku }), " ", E({ $b.subbuf-rw(0, 1) = (8, 8); $b.raku }), " ", E({ $b.subbuf-rw(0, 1) = 8; $b.raku }), " ", E({ $b.subbuf-rw(0, 1) = "x"; $b.raku }), " ", E({ $b.subbuf-rw(1, 1) = Buf.new(1); $b.raku }), " ", E({ $b.subbuf-rw(9, 1) = Buf.new(1); $b.raku }), " ", E({ my $i = -1; $b.subbuf-rw($i, 1) = Buf.new(1); $b.raku }), " ", E({ $b.subbuf-rw(0, 1) = utf8.new(65); $b.raku }), " ", E({ Blob.new(1).subbuf-rw(0, 1) }), " ", E({ my $p := $b.subbuf-rw(0, 1); $p = Buf.new(2, 2); $p.raku }), " ", $b.raku, " ", E({ subbuf-rw(Blob.new(1)) }), " ", E({ $b.subbuf-rw(1, 0) = blob16.new(300); $b.raku }), " ", E({ $b.subbuf-rw(0, 5) = Buf.new(1); $b.raku }), " ", E({ $b.subbuf-rw(1, 1) = Blob.new(2, 3, 4); $b.raku }), " ", E({ $b.subbuf-rw(2, 0).raku }), " ", E({ $b.subbuf-rw(1, -1) = Buf.new(7); $b.raku })
# rakudo 2026.08: ok:"Buf.new(0,1,2,100,101,4,5)" ok:"Buf.new(0,1,2,200)" ok:"Buf.new(7,7)" ok:"Buf.new(1,7,7)" ok:"Buf.new(1,7,9,7)" ok:"Buf.new(1,7)" ok:"Buf.new(7)" ok:"Buf" ok:"Buf.new(1,5,5)" ok:"Buf.new(1,3)" ok:"Buf.new(4)" T:X::TypeCheck::Binding::Parameter T:X::TypeCheck::Binding::Parameter T:X::TypeCheck::Binding::Parameter ok:"Buf.new(4,1)" T:X::TypeCheck::Assignment T:X::TypeCheck::Assignment ok:"Buf.new(65,1)" T:X::Method::NotFound ok:"Buf.new(65)" Buf.new(2,2,1) T:X::Multi::NoMatch ok:"Buf.new(2,44,2,1)" ok:"Buf.new(1)" ok:"Buf.new(1,2,3,4)" ok:"Buf.new()" T:X::TypeCheck::Assignment
```
rakupp 4.0.1-84: differs — a list, Int or Str on the right is accepted, a negative start or count is accepted, a start past the end throws `X::OutOfRange`, the Proxy reads back the new value, `Blob.subbuf-rw` answers a Blob, and the sub form does not exist.

### BB-23  reallocate                                                D:yes R:yes V:spec
`reallocate(n)` sets the element count, padding with zeros or dropping
the tail, and returns the buffer itself; shrinking then growing gives
zeros (no leftovers). A Bool counts as 0/1. A non-Int argument is
`X::TypeCheck::Binding::Parameter`, a negative one throws `X::AdHoc`
and leaves the buffer unchanged; on the `Buf` type object it is
`X::Parameter::InvalidConcreteness`, on a Blob `X::Method::NotFound`.
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; my $b = Buf.new(1,2,3); say E({ $b.reallocate(5).raku }), " ", E({ $b.reallocate(2).raku }), " ", E({ $b.reallocate(0).raku }), " ", E({ $b.reallocate(3).raku }), " ", E({ $b.reallocate(3) === $b }), " ", E({ $b.reallocate(2.5) }), " ", E({ Buf.allocate(3, (1,2,3)).reallocate(0).reallocate(2).raku }), " ", E({ Blob.new(1).reallocate(2) }), " ", E({ Buf.reallocate(2) }), " ", E({ Buf.new.reallocate(2).raku }), " ", E({ buf16.new(1).reallocate(2).raku }), " ", E({ $b.reallocate("4").raku }), " ", E({ $b.reallocate(3).^name }), " ", E({ Buf.new(1,2,3).reallocate(1).reallocate(3).raku }), " ", E({ $b.reallocate(True).raku })
# rakudo 2026.08: ok:"Buf.new(1,2,3,0,0)" ok:"Buf.new(1,2)" ok:"Buf.new()" ok:"Buf.new(0,0,0)" ok:Bool::True T:X::TypeCheck::Binding::Parameter ok:"Buf.new(0,0)" T:X::Method::NotFound T:X::Parameter::InvalidConcreteness ok:"Buf.new(0,0)" ok:"Buf[uint16].new(1,0)" T:X::TypeCheck::Binding::Parameter ok:"Buf" ok:"Buf.new(1,0,0)" ok:"Buf.new(0)"
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; my $b = Buf.new(1,2,3); say E({ $b.reallocate(-1) }), " ", $b.elems, " ", $b.raku
# rakudo 2026.08: T:X::AdHoc 3 Buf.new(1,2,3)
```
rakupp 4.0.1-84: differs — a fresh `Buf.new.reallocate(2)`, a `buf16` and a chained `.reallocate(0).reallocate(2)` are `X::Assignment::RO`, a Rat/Str argument is accepted, the type object is `X::Method::NotFound`, and `reallocate(-1)` crashes the process ("Internal error: basic_string").

## F. Encoding and decoding

### BB-24  Str.encode: result types and encoding names               D:yes R:yes V:bug
`encode` (also on any Cool, via its Str) returns a `utf8` for utf8, a
`utf16` for utf16 (16-bit units, so `.bytes` is twice `.elems`), and a
`Blob[uint8]` for every other encoding, including `utf16le`/`utf16be`
(bytes, no BOM in any utf16 form) and `utf8-c8`; only the `utf8`/`utf16`
results answer `.encoding`. Names are matched case-insensitively: `utf8`,
`utf-8`; `utf8-c8`, `utf8c8`, `utf-8-c8`; `utf16`, `utf-16`; `utf16le`,
`utf-16le`, `utf16-le`, `utf-16-le` (and `be`); `ascii`; `iso-8859-1`,
`iso_8859-1:1987`, `iso_8859-1`, `iso-ir-100`, `latin1`, `latin-1`,
`csisolatin1`, `l1`, `ibm819`, `cp819`; `windows-1251`/`windows1251`,
`windows-1252`/`windows1252`, `windows-932`/`windows932`, `gb2312`,
`gb18030`. Anything else — `utf32`, `utf-32`, `""`, a name with a
trailing space — throws `X::Encoding::Unknown` with `.name` as given;
a `:enc` named argument is ignored. `"".encode` is an empty `utf8`. A
second positional throws `X::AdHoc`. THE BUG: a non-Str encoding
argument (`"a".encode(42)`, `.encode(Str)`) recurses forever.
```
say "a".encode.^name, " ", "a".encode.raku, " ", ("a".encode ~~ blob8), " ", ("a".encode ~~ Blob[uint8]), " ", "a".encode("ascii").^name, " ", "a".encode("latin1").^name, " ", "a".encode("windows-1252").^name, " ", "a".encode("utf8-c8").^name, " ", "a".encode("utf16").^name, " ", "a".encode("utf16").raku, " ", "a".encode("utf16le").^name, " ", "a".encode("utf16le").list.raku, " ", "a".encode("utf16be").list.raku, " ", "a".encode("utf16").bytes, " ", "a".encode("utf16").elems, " ", (try "a".encode("utf32").^name) // $!.^name, " ", (try "a".encode("nope")) // $!.^name ~ ":" ~ ((try $!.name) // "-"), " ", "a".encode("UTF-8").^name, " ", "a".encode("Utf8").^name, " ", "a".encode("ISO-8859-1").^name, " ", "a".encode("l1").list.raku, " ", "a".encode("cp819").list.raku, " ", "a".encode("windows1252").list.raku, " ", "a".encode("utf-16-le").list.raku, " ", "a".encode("UTF16BE").list.raku, " ", "a".encode("utf8c8").^name, " ", "a".encode("windows-1251").^name, " ", "a".encode("windows-932").^name, " ", "a".encode("gb2312").^name, " ", "a".encode("gb18030").^name, " ", 42.encode.raku, " ", 42.encode.^name, " ", (try "a".encode("utf8").encoding) // $!.^name, " ", (try "a".encode("ascii").encoding.raku) // $!.^name, " ", (try "a".encode("utf16").encoding) // $!.^name, " ", (try "a".encode("utf16le").encoding.raku) // $!.^name, " ", "".encode.raku, " ", "".encode.elems, " ", (try "a".encode("")) // $!.^name, " ", "a".encode(:enc<ascii>).^name, " ", (try "a".encode("ASCII ").^name) // $!.^name, " ", "ab".encode("utf16").list.raku, " ", "a".encode("UTF-16LE").^name, " ", "a".encode("Utf-8-C8").^name, " ", (try "a".encode("utf-32").^name) // $!.^name, " ", (try "a".encode("utf8-c8").encoding.raku) // $!.^name, " ", "abc".encode.bytes, " ", "a".encode("utf16").WHICH.^name, " ", (try "a".encode("utf8", "utf8")) // $!.^name, " ", (try "a".encode.Buf.^name) // $!.^name, " ", (try "a".encode("utf16").Buf.^name) // $!.^name, " ", (try "a".encode("ascii").Buf.^name) // $!.^name
# rakudo 2026.08: utf8 utf8.new(97) True True Blob[uint8] Blob[uint8] Blob[uint8] Blob[uint8] utf16 utf16.new(97) Blob[uint8] (97, 0) (0, 97) 2 1 X::Encoding::Unknown X::Encoding::Unknown:nope utf8 utf8 Blob[uint8] (97,) (97,) (97,) (97, 0) (0, 97) Blob[uint8] Blob[uint8] Blob[uint8] Blob[uint8] Blob[uint8] utf8.new(52,50) utf8 utf-8 Any utf-16 Any utf8.new() 0 X::Encoding::Unknown utf8 X::Encoding::Unknown (97, 98) Blob[uint8] Blob[uint8] X::Encoding::Unknown Any 3 ValueObjAt X::AdHoc Buf Buf[uint16] Buf
say "a".encode(42).^name
# rakudo 2026.08: (no output: killed by the 10-second alarm)
```
rakupp 4.0.1-84: differs — every encoding returns a `utf8` (ascii, latin1, the windows and gb ones, utf8-c8, utf16le/be), `"a".encode ~~ Blob[uint8]` is False, `utf16le`/`utf16be` give code units, `utf32` is accepted, an unknown or empty name returns a utf8 instead of throwing, `.encoding` and `.Buf` are missing, and `encode(42)` returns a utf8.

### BB-25  encode: multi-byte text, refusals, :replacement, :strict     D:partial R:yes V:spec
utf8 output is NFC (`e\x[301]` encodes as the two bytes of U+00E9,
under `utf8-c8` too); a non-BMP character is four bytes in utf8 and a
surrogate pair in every utf16 form; a BOM character inside the text is
kept; noncharacters, U+10FFFF and NUL encode. A character the encoding
lacks (U+263A in ascii, latin1, windows-1252; U+0080 in ascii; U+0100 in
latin1) throws `X::AdHoc`; `:replacement` substitutes instead: `True`
or bare `:replacement` means `?` (`\x[FFFD]` for a utf encoding), a Str
is used as given (may be empty or several characters), any other value
is stringified. `:strict` refuses a codepoint the encoding leaves
unmapped (U+0081 in windows-1252) that is otherwise passed through as
its byte, and `:strict` WINS over `:replacement`; `:replacement(False)`
is no replacement. `:translate-nl` changes nothing on a non-Windows
host.
```
say "\x[e9]".encode.list.raku, " ", "\x[e9]".encode.elems, " ", "\x[e9]".encode("latin1").list.raku, " ", "\x[1F600]".encode.list.raku, " ", "\x[1F600]".encode("utf16").list.raku, " ", "\x[1F600]".encode("utf16").elems, " ", "\x[1F600]".encode("utf16le").list.raku, " ", "\x[1F600]".encode("utf16be").list.raku, " ", "a\x[FEFF]".encode("utf16").list.raku, " ", "\x[20AC]".encode("windows-1252").list.raku, " ", "\x[416]".encode("windows-1251").list.raku, " ", (try "\x[263A]".encode("ascii")) // $!.^name, " ", (try "\x[263A]".encode("latin1")) // $!.^name, " ", (try "\x[263A]".encode("windows-1252")) // $!.^name, " ", "\x[263A]".encode("ascii", :replacement).list.raku, " ", "\x[263A]".encode("ascii", :replacement("XYZ")).list.raku, " ", "\x[263A]".encode("ascii", :replacement("")).list.raku, " ", "\x[263A]".encode("latin1", :replacement).list.raku, " ", "a\x[81]".encode("windows-1252").list.raku, " ", (try "a\x[81]".encode("windows-1252", :strict).list.raku) // $!.^name, " ", "a\x[81]".encode("windows-1252", :!strict).list.raku, " ", "a\nb".encode("utf8", :translate-nl).list.raku, " ", "a\r\nb".encode(:translate-nl).elems, " ", "a".encode("utf8", :replacement("?")).raku, " ", (try "\x[FFFE]".encode.list.raku) // $!.^name, " ", "\x[10FFFF]".encode.list.raku, " ", "e\x[301]".encode.list.raku, " ", "e\x[301]".encode("utf8-c8").list.raku, " ", "\x[e9]".encode("utf8-c8").list.raku, " ", "ab".encode("utf16", :replacement).list.raku, " ", (try "\x[263A]".encode("ascii", :strict)) // $!.^name, " ", (try "\x[263A]".encode("ascii", :replacement, :strict).list.raku) // $!.^name, " ", "\x[263A]".encode("utf8", :replacement("?")).list.raku, " ", "\x[FFFD]".encode("ascii", :replacement).list.raku, " ", "\x[263A]".encode("windows-1252", :replacement).list.raku, " ", "\x[263A]".encode("windows-1251", :replacement("*")).list.raku, " ", (try "\x[263A]".encode("ascii", :replacement(42)).list.raku) // $!.^name, " ", (try "\x[263A]".encode("ascii", :replacement(False)).list.raku) // $!.^name, " ", "\x[263A]".encode("ascii", :replacement(True)).list.raku, " ", "\x[FFFF]".encode.list.raku, " ", "\x[0]".encode.list.raku, " ", "\x[7F]".encode("ascii").list.raku, " ", (try "\x[80]".encode("ascii")) // $!.^name, " ", "\x[FF]".encode("latin1").list.raku, " ", (try "\x[100]".encode("latin1")) // $!.^name, " ", "\x[1F600]".encode("utf8-c8").list.raku, " ", "a".encode("utf16", :translate-nl).list.raku, " ", "\x[e9]".encode("utf16").bytes, " ", "\x[e9]".encode("utf8").^name
# rakudo 2026.08: (195, 169) 2 (233,) (240, 159, 152, 128) (55357, 56832) 2 (61, 216, 0, 222) (216, 61, 222, 0) (97, 65279) (128,) (198,) X::AdHoc X::AdHoc X::AdHoc (63,) (88, 89, 90) () (63,) (97, 129) X::AdHoc (97, 129) (97, 10, 98) 4 utf8.new(97) (239, 191, 190) (244, 143, 191, 191) (195, 169) (195, 169) (195, 169) (97, 98) X::AdHoc (63,) (226, 152, 186) (63,) (63,) (42,) (52, 50) X::AdHoc (63,) (239, 191, 191) (0,) (127,) X::AdHoc (255,) X::AdHoc (240, 159, 152, 128) (97,) 2 utf8
```
rakupp 4.0.1-84: differs — utf16le/be give code units, windows-1251 and gb2312 produce utf8 bytes, an unencodable character is emitted as its utf8 bytes under ascii (also `\x[80]`) and silently replaced by `?` under latin1 and windows-1252 (also `\x[100]`), `:replacement("")`, `:replacement("*")` and `:replacement(False)` are ignored (a `?` or the utf8 bytes come out), and `:strict` never refuses.

### BB-26  Blob.decode                                                D:partial R:partial V:quirk
`decode` defaults to the buffer's own encoding (`utf8`, `utf16` — a
`utf32` cannot be decoded, `X::AdHoc`), else utf-8; a type object as
the name also means utf-8 and a `:enc` named argument is ignored. Names
normalise as for `encode`, but an unknown name is `X::AdHoc`, not
`X::Encoding::Unknown`, and an EMPTY buffer decodes to `""` under any
name at all. Malformed utf8 (a lone continuation byte, a truncated
sequence, an overlong form, an encoded surrogate, a codepoint above
U+10FFFF), invalid ascii and invalid utf16 (an odd byte count, a lone
surrogate) throw `X::AdHoc`, and `:replacement` does NOT rescue them (it
is honoured only for an unmapped byte of an 8-bit table encoding, and
only together with `:strict`: without `:strict` such a byte decodes to
its own codepoint). A leading utf8 BOM is dropped; utf16 honours a BOM
of either endianness and is host-endian without one; `utf16le`/`utf16be`
fix the order. utf8 output is NFC (`101,204,129` is one char, U+00E9);
`utf8-c8` keeps every byte round-trippable, never throws, and a
combining sequence or invalid byte comes back as a synthetic grapheme.
`\r\n` is not translated. A 16- or 32-bit buffer decodes its STORAGE
bytes (`blob16.new(97,98).decode` is `"a\0b\0"` on a little-endian host).
```
say Buf.new(195,182).decode.ord, " ", Buf.new(195,182).decode("utf8").ord, " ", (try Buf.new(246).decode("latin1").ord) // $!.^name, " ", (try Buf.new(246).decode("ISO-8859-1").ord) // $!.^name, " ", (try Buf.new(0x80).decode("windows-1252").ord) // $!.^name, " ", (try Buf.new(0xC0).decode("windows-1251").ord) // $!.^name, " ", (try Buf.new(0x81).decode("windows-1252").ord) // $!.^name, " ", (try Buf.new(0x81).decode("windows-1252", :strict).ord) // $!.^name, " ", (try Buf.new(255).decode) // $!.^name, " ", (try Buf.new(255).decode("utf8")) // $!.^name, " ", (try Buf.new(195).decode) // $!.^name, " ", (try Buf.new(200).decode("ascii")) // $!.^name, " ", Buf.new(255).decode("utf8-c8").chars, " ", Buf.new(255).decode("utf8-c8").encode("utf8-c8").list.raku, " ", (try Buf.new(255).decode("utf8", :replacement("?")).raku) // $!.^name, " ", (try Buf.new(255, 97).decode("utf8", :replacement("\x[FFFD]")).ords.raku) // $!.^name, " ", (try Buf.new(255).decode("utf8", :replacement("?"), :strict).raku) // $!.^name, " ", Buf.new(255,254,72,0,101,0).decode("utf-16").raku, " ", Buf.new(72,0,101,0).decode("utf16").raku, " ", Buf.new(0,72,0,101).decode("utf16be").raku, " ", Buf.new(72,0,101,0).decode("utf16le").raku, " ", Buf.new(254,255,0,72).decode("utf16").raku, " ", (try Buf.new(72,0,101).decode("utf16")) // $!.^name, " ", (try Buf.new(0,0xD8).decode("utf16le")) // $!.^name, " ", utf16.new(0xD83D, 0xDE00).decode.ords.raku, " ", utf16.new(97).decode.raku, " ", (try Buf.new(1).decode("nope")) // $!.^name, " ", (try Buf.new(1).decode("utf32")) // $!.^name, " ", Buf.new.decode.raku, " ", Blob.new.decode("nope").raku, " ", (try Blob.decode) // $!.^name, " ", "abc".encode.decode.raku, " ", utf8.new(97,98).decode.raku, " ", utf8.new(97).decode("latin1").raku, " ", (try blob16.new(97,98).decode.raku) // $!.^name, " ", (try blob16.new(97,98).decode("latin1").raku) // $!.^name, " ", (try blob32.new(97).decode.raku) // $!.^name, " ", Blob[int8].new(97,-1).decode("latin1").ords.raku, " ", Buf.new(97,0,98).decode.ords.raku, " ", Buf.new(0xEF,0xBB,0xBF,97).decode.ords.raku, " ", Buf.new(101,204,129).decode.ords.raku, " ", Buf.new(101,204,129).decode.chars, " ", Buf.new(101,204,129).decode("utf8-c8").ords.raku, " ", Buf.new(0xC3,0xA9).decode("UTF-8").ords.raku, " ", Buf.new(97).decode("Latin-1").raku, " ", Buf.new(255).decode("utf8-c8").ords.raku, " ", Buf.new(97).decode(:enc<latin1>).raku, " ", (try Buf.new(97).decode(Str)) // $!.^name, " ", Buf.new(97).decode("utf8", :translate-nl).raku, " ", Buf.new(97, 13, 10).decode.ords.raku, " ", (try Buf.new(0xE2, 0x82).decode("utf8", :replacement("?")).raku) // $!.^name, " ", (try Buf.new(0xC0, 0x80).decode) // $!.^name, " ", (try Buf.new(0xED, 0xA0, 0x80).decode) // $!.^name, " ", (try Buf.new(0xF4, 0x90, 0x80, 0x80).decode) // $!.^name, " ", (try Buf.new(0x9F).decode("windows-1252", :strict).ord) // $!.^name, " ", (try Buf.new(255).decode("ascii", :replacement("?")).raku) // $!.^name, " ", (try Buf.new(0x81).decode("windows-1252", :replacement("?")).raku) // $!.^name, " ", (try Buf.new(0x81).decode("windows-1252", :strict, :replacement("?")).raku) // $!.^name, " ", (try Buf.new(0x98).decode("windows-1251").ord) // $!.^name, " ", Buf.new(97).decode.^name, " ", Buf.new(97).decode("utf8-c8").^name, " ", (try Buf.new(1,2).decode("utf16", :replacement("?")).ord) // $!.^name, " ", Buf.new(72, 0).decode("utf16").raku, " ", (try Buf.new(255).decode("windows-1252").ord) // $!.^name, " ", (try Buf.new(0x9F).decode("windows-1252").ord) // $!.^name
# rakudo 2026.08: 246 246 246 246 8364 1040 129 X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc 1 (255,) X::AdHoc X::AdHoc X::AdHoc "He" "He" "He" "He" "H" X::AdHoc X::AdHoc (128512,).Seq "a" X::AdHoc X::AdHoc "" "" X::Multi::NoMatch "abc" "ab" "a" "a\0b\0" "a\0b\0" "a\0\0\0" (97, 255).Seq (97, 0, 98).Seq (97,).Seq (233,).Seq 1 (101, 1114109, 120, 67, 67, 1114109, 120, 56, 49).Seq (233,).Seq "a" (1114109, 120, 70, 70).Seq "a" a "a" (97, 13, 10).Seq X::AdHoc X::AdHoc X::AdHoc X::AdHoc 376 X::AdHoc "\x[81]" "?" 152 Str Str 513 "H" 255 376
```
rakupp 4.0.1-84: differs — windows-1251 decodes as utf8, `:strict` and the strict-only `:replacement` are ignored, an odd-length utf16, a lone surrogate, an overlong `F4 90 ..` and an unknown name (`nope`, `utf32`) all decode without error, `Blob.decode` is `X::Method::NotFound`, a 16-bit buffer decodes its elements as codepoints (`"ab"`), the utf8 BOM is kept, and `utf8-c8` normalises instead of preserving the bytes.

### BB-27  Encoding::Registry, encoders and decoders                  D:yes R:yes V:spec
`Encoding::Registry.find($name)` is case-insensitive and returns the
same `Encoding::Builtin` for a name and each of its alternatives (`.name`
is the canonical one: `utf8`, `iso-8859-1`, `windows-1251`; `.alternative-
names` the rest, `()` for ascii; `utf32` is not registered); an unknown
name throws `X::Encoding::Unknown` (`.name`); `Encoding::Builtin.new` is
`X::Cannot::New`. `.encoder(:replacement, :translate-nl, :strict)`
gives an `Encoding::Encoder::Builtin` whose `encode-chars` returns the
buffer types of BB-24 and refuses like BB-25. `.decoder` gives an
`Encoding::Decoder::Builtin`: `add-bytes(Blob)` (a Str is
`X::TypeCheck::Binding::Parameter`), `consume-available-chars` (holds
back a character that a following byte could still combine with, so a
complete `C3 A9` yields `""` until more arrives or `consume-all-chars`),
`consume-all-chars`, `consume-exactly-chars(n, :eof)` and
`consume-line-chars(:chomp, :eof)` return the Str type object when the
request cannot be met yet, `consume-exactly-bytes(n)` a `Buf[uint8]` or
the `Blob` type object, `set-line-separators(@seps)`, `bytes-available`,
`is-empty`; `:translate-nl` turns `\r\n` into `\n`; a malformed byte
throws `X::AdHoc` at consume time. `register` takes a class doing
`Encoding`, returns Nil, registers the name and alternatives case-
insensitively, throws `X::Encoding::AlreadyRegistered` (`.name`) for any
clash including the built-ins; `Str.encode` then finds the new encoding.
```
say Encoding::Registry.find("UTF-8").name, " ", Encoding::Registry.find("utf8").^name, " ", (Encoding::Registry.find("utf8") ~~ Encoding), " ", Encoding::Registry.find("latin1").name, " ", (try Encoding::Registry.find("latin1").alternative-names.raku) // $!.^name, " ", (try Encoding::Registry.find("utf8").alternative-names.raku) // $!.^name, " ", (try Encoding::Registry.find("utf16").alternative-names.raku) // $!.^name, " ", (try Encoding::Registry.find("utf16le").alternative-names.raku) // $!.^name, " ", (try Encoding::Registry.find("ascii").alternative-names.raku) // $!.^name, " ", (try Encoding::Registry.find("utf8-c8").alternative-names.raku) // $!.^name, " ", (try Encoding::Registry.find("windows-1252").alternative-names.raku) // $!.^name, " ", (try Encoding::Registry.find("utf32").name) // $!.^name, " ", (try Encoding::Registry.find("utf-29")) // $!.^name ~ ":" ~ ((try $!.name) // "-"), " ", Encoding::Registry.find("ascii").encoder.encode-chars("foo").raku, " ", Encoding::Registry.find("utf8").encoder.encode-chars("foo").raku, " ", Encoding::Registry.find("utf16").encoder.encode-chars("a").raku, " ", (try Encoding::Registry.find("ascii").encoder.encode-chars("\x[a3]")) // $!.^name, " ", Encoding::Registry.find("ascii").encoder(:replacement).encode-chars("\x[a3]").raku, " ", Encoding::Registry.find("ascii").encoder(:replacement("f")).encode-chars("\x[a3]").raku, " ", Encoding::Registry.find("utf8").decoder.^name, " ", (Encoding::Registry.find("utf8").decoder ~~ Encoding::Decoder), " ", (try do { my $d = Encoding::Registry.find("utf8").decoder; $d.add-bytes("ab\ncd".encode); ($d.consume-line-chars(:chomp).raku, $d.bytes-available, $d.consume-all-chars.raku, $d.is-empty).raku }) // $!.^name, " ", (try Encoding::Builtin.new) // $!.^name, " ", (try Encoding::Registry.find("Windows-1251").name) // $!.^name, " ", (try Encoding::Registry.find("windows932").name) // $!.^name, " ", (try Encoding::Registry.find("gb18030").name) // $!.^name, " ", (try Encoding::Registry.find("iso_8859-1").name) // $!.^name, " ", (try do { my $d = Encoding::Registry.find("utf8").decoder; $d.add-bytes(Blob.new(0xC3)); my $a = $d.consume-available-chars.raku; $d.add-bytes(Blob.new(0xA9)); ($a, $d.consume-available-chars.ords.raku).raku }) // $!.^name, " ", (try do { my $d = Encoding::Registry.find("utf8").decoder(:translate-nl); $d.add-bytes("a\r\nb".encode); $d.consume-all-chars.ords.raku }) // $!.^name, " ", (try do { my $d = Encoding::Registry.find("utf8").decoder; $d.add-bytes("abcd".encode); ($d.consume-exactly-chars(2).raku, $d.consume-exactly-chars(5).raku, $d.consume-exactly-chars(5, :eof).raku).raku }) // $!.^name, " ", (try do { my $d = Encoding::Registry.find("utf8").decoder; $d.add-bytes("abcd".encode); ($d.consume-exactly-bytes(2).raku, $d.consume-exactly-bytes(5).raku).raku }) // $!.^name, " ", (try do { my $d = Encoding::Registry.find("utf8").decoder; $d.set-line-separators(["|"]); $d.add-bytes("a|b".encode); ($d.consume-line-chars.raku, $d.consume-line-chars.raku, $d.consume-line-chars(:eof).raku).raku }) // $!.^name, " ", (try do { my $d = Encoding::Registry.find("utf8").decoder; $d.add-bytes(Blob.new(255)); $d.consume-all-chars }) // $!.^name, " ", (try Encoding::Registry.find("utf8").decoder.add-bytes("x")) // $!.^name, " ", Encoding::Registry.find("ascii").encoder.^name, " ", (Encoding::Registry.find("ascii").encoder ~~ Encoding::Encoder), " ", (try Encoding::Registry.register(Encoding::Registry.find("utf8"))) // $!.^name ~ ":" ~ ((try $!.name) // "-"), " ", (try do { my class E29 does Encoding { method name() { "utf-29" }; method alternative-names() { ("u29",) }; method encoder(|) { die "NYI" }; method decoder(|) { die "NYI" } }; Encoding::Registry.register(E29).raku ~ " " ~ Encoding::Registry.find("U29").^name ~ " " ~ Encoding::Registry.find("UTF-29").name ~ " " ~ ((try Encoding::Registry.register(E29)) // $!.^name) }) // $!.^name, " ", (try "a".encode("u29")) // $!.^name
# rakudo 2026.08: utf8 Encoding::Builtin True iso-8859-1 ("iso_8859-1:1987", "iso_8859-1", "iso-ir-100", "latin1", "latin-1", "csisolatin1", "l1", "ibm819", "cp819") ("utf-8",) ("utf-16",) ("utf-16le", "utf16-le", "utf-16-le") () ("utf8c8", "utf-8-c8") ("windows1252",) X::Encoding::Unknown X::Encoding::Unknown:utf-29 Blob[uint8].new(102,111,111) utf8.new(102,111,111) utf16.new(97) X::AdHoc Blob[uint8].new(63) Blob[uint8].new(102) Encoding::Decoder::Builtin True ("\"ab\"", 2, "\"cd\"", Bool::True) X::Cannot::New windows-1251 windows-932 gb18030 iso-8859-1 ("\"\"", "().Seq") (97, 10, 98).Seq ("\"ab\"", "Str", "\"cd\"") ("Buf[uint8].new(97,98)", "Blob") ("\"a|\"", "Str", "\"b\"") X::AdHoc X::TypeCheck::Binding::Parameter Encoding::Encoder::Builtin True X::Encoding::AlreadyRegistered:utf8 Nil E29 utf-29 X::Encoding::AlreadyRegistered X::AdHoc
```
rakupp 4.0.1-84: differs — `.name` is the name as given, `.alternative-names` and `Encoding::Builtin.new` are missing, `gb18030` and `iso_8859-1` are unknown, `encode-chars` returns a plain `Blob` and never refuses or replaces, the decoder is not an `Encoding::Decoder`, `consume-exactly-chars` is missing, `:translate-nl` on the decoder is ignored, a malformed byte in `consume-all-chars` prints a raw error text into the output (in one run it killed the process there), `register` of an already registered encoding returns Nil instead of throwing, and a registered encoding is found by name but not used by `Str.encode`.

### BB-28  utf8, utf16 and utf32 as strings                           D:partial R:partial V:spec
A `utf8`/`utf16` decodes itself wherever a Str is wanted: `.Str`,
`.Stringy`, prefix `~`, interpolation, `~` with a Str, `x`, `eq`/`lt`/
`cmp` against a Str, and `~~ "a"` all use the text; a `utf32` has the
`.encoding` but cannot decode (`X::AdHoc`), and a malformed `utf8`
throws `X::AdHoc` from `.Str`. Everything else stays a Blob: `.chars`
throws `X::Buf::AsStr`, `.Numeric`/`+`/`==` are the element count, `.uc`,
`.fmt`, `.contains` do not exist, `.join` joins the numbers, `~~ Str` is
False, `"a" ~~ utf8.new(97)` is False, `.gist`/`.raku` are `utf8:0x<61>`
and `utf8.new(97)`, `.new` refuses non-Ints and wraps 256 to 0, `.WHICH`
starts with `utf8|`, `.subbuf`/`.reverse` stay `utf8`, `~` between two
`utf8` stays `utf8` and with a `utf16` gives a `Buf`, `.Buf` is a plain
`Buf` (`Buf[uint16]` for utf16), `.Blob` and `push` do not exist.
```
say (try utf8.new(97,98).Str) // $!.^name, " ", (try utf8.new(97,98).Stringy) // $!.^name, " ", (try "x{utf8.new(97)}y") // $!.^name, " ", (try ~utf16.new(97)) // $!.^name, " ", (try utf8.new(97) eq "a") // $!.^name, " ", (try utf8.new(97) ~~ "a") // $!.^name, " ", (try "a" ~~ utf8.new(97)) // $!.^name, " ", (try utf8.new(97,98) x 2) // $!.^name, " ", (try utf8.new(97).chars) // $!.^name, " ", (try utf8.new(255).Str) // $!.^name, " ", (try utf8.new("abc")) // $!.^name, " ", utf8.new(256).list.raku, " ", utf8.new(97).gist, " ", utf8.new(97).raku, " ", (utf8.new(97) eqv "a".encode), " ", ("a".encode eqv blob8.new(97)), " ", utf8.new(97).elems, " ", (try utf8.new(97).encoding) // $!.^name, " ", utf8.new(97).decode("latin1"), " ", (try utf16.new(97).Str) // $!.^name, " ", (try utf32.new(97).encoding) // $!.^name, " ", (try utf32.new(97).Str) // $!.^name, " ", utf8.new(97).WHICH.Str.subst(/\|.*/, "|..."), " ", (try (utf8.new(97) ~ utf8.new(98)).^name) // $!.^name, " ", (try (utf8.new(97) ~ "b").^name) // $!.^name, " ", (try utf8.new(97).Buf.^name) // $!.^name, " ", (try utf8.new(97).Blob.^name) // $!.^name, " ", (try utf8.new(97) cmp "b") // $!.^name, " ", utf8.new(97).subbuf(0).^name, " ", utf8.new(97).reverse.^name, " ", (try utf8.new(97).push(1)) // $!.^name, " ", (try utf8.new(1,2).Str.ords.raku) // $!.^name, " ", (try ("a".encode ~ "b".encode).Str) // $!.^name, " ", (try utf8.new(0xC3, 0xA9).Str.ords.raku) // $!.^name, " ", (try utf8.new(97).Str.^name) // $!.^name, " ", (utf8.new(97) eq utf8.new(97)), " ", (utf8.new(97) eq blob8.new(97)), " ", (try utf8.new(97) eq "b") // $!.^name, " ", (try utf8.new(97) lt "b") // $!.^name, " ", (try utf8.new(97).uc) // $!.^name, " ", (try utf8.new(97,98).Str.chars) // $!.^name, " ", (try utf8.new(97).Numeric) // $!.^name, " ", (try utf8.new(97) == 1) // $!.^name, " ", (try +utf8.new(97)) // $!.^name, " ", (try utf8.new.Str.raku) // $!.^name, " ", (try utf16.new(0xD83D, 0xDE00).Str.ords.raku) // $!.^name, " ", (try utf16.new(0xD800).Str) // $!.^name, " ", utf16.new(97).gist, " ", (try utf16.new(97).Buf.^name) // $!.^name, " ", (try (utf16.new(97) ~ utf16.new(98)).Str) // $!.^name, " ", (try (utf16.new(97) ~ utf8.new(98)).^name) // $!.^name, " ", (try utf8.new(97).Str.encode.^name) // $!.^name, " ", (utf8.new(97) ~~ Str), " ", (utf8.new(97) ~~ Stringy), " ", (try utf8.new(97,10).Str.raku) // $!.^name, " ", (try utf8.new(97).fmt("%s")) // $!.^name, " ", utf8.new(97).join("-"), " ", utf8.new(97,98).sort.raku, " ", utf8.new(97).list.raku, " ", (try utf8.new(97).contains("a")) // $!.^name
# rakudo 2026.08: ab ab xay a True True False abab X::Buf::AsStr X::AdHoc X::TypeCheck (0,) utf8:0x<61> utf8.new(97) True False 1 utf-8 a a utf-32 X::AdHoc utf8|... utf8 Str Buf X::Method::NotFound Less utf8 utf8 X::Multi::NoMatch (1, 2).Seq ab (233,).Seq Str True True False True X::Method::NotFound 2 1 True 1 "" (128512,).Seq X::AdHoc utf16:0x<0061> Buf[uint16] ab Buf utf8 False True "a\n" X::Multi::NoMatch 97 (97, 98).Seq (97,) X::Method::NotFound
```
rakupp 4.0.1-84: differs — `.Str`, `.Stringy`, `.Str.ords`, `(utf8 ~ utf8).Str` and `utf16.Str` throw `X::Buf::AsStr` while interpolation, prefix `~` (on a utf16 sometimes with a trailing NUL: `a` or `a\0` across runs), `eq`, `x`, `cmp`, `~ Str` and `.uc` do decode; `.chars` answers 1; `.Numeric` is `X::Str::Numeric`; `"a" ~~ utf8.new(97)` is True; `.encoding`/`.Buf`/`.Blob` are missing; `utf8.new("abc")` is `Blob[uint8]:0x<00>`; and `.reverse` is a Seq.

## G. Binary reads and writes

### BB-29  read-int8 … read-uint128 with an Endian                    D:yes R:yes V:quirk
`read-{int,uint}{8,16,32,64,128}($offset, $endian = NativeEndian)`
read that many bytes at a BYTE offset and return an `Int` (signed
two's complement or unsigned; 64-bit unsigned and the 128-bit forms are
big Ints). `Endian` is an enum `NativeEndian` 0, `LittleEndian` 1,
`BigEndian` 2; this host is little-endian. An offset past the end
(`elems - width + 1`), a negative offset, a Rat or Str offset, or the
`Endian` type object throw `X::AdHoc`; an Int or Str where an `Endian`
is expected is `X::TypeCheck::Binding::Parameter`; on a type object it
is `X::Parameter::InvalidConcreteness`. The quirk: the methods exist on
EVERY Blob and read the raw storage bytes, so `blob16.new(1,2).read-
uint16(0, BigEndian)` is 256.
```
my $b = blob8.new(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,255); say $b.read-uint8(0), " ", $b.read-uint8(16), " ", $b.read-int8(16), " ", $b.read-uint16(0, BigEndian), " ", $b.read-uint16(0, LittleEndian), " ", $b.read-uint16(15, BigEndian), " ", $b.read-int16(15, BigEndian), " ", $b.read-int16(15, LittleEndian), " ", $b.read-uint32(0, BigEndian), " ", $b.read-uint32(0, LittleEndian), " ", $b.read-int32(13, LittleEndian), " ", $b.read-uint64(0, BigEndian), " ", $b.read-uint64(0, LittleEndian), " ", $b.read-int64(9, LittleEndian), " ", $b.read-uint128(0, BigEndian), " ", $b.read-uint128(0, LittleEndian), " ", $b.read-int128(1, LittleEndian), " ", blob8.new(255 xx 8).read-uint64(0), " ", blob8.new(255 xx 8).read-int64(0), " ", blob8.new(255 xx 16).read-uint128(0), " ", blob8.new(255 xx 16).read-int128(0), " ", $b.read-uint8(0).^name, " ", $b.read-uint64(0).^name, " ", $b.read-uint16(0) == $b.read-uint16(0, NativeEndian), " ", Kernel.endian, " ", (try $b.read-uint8(17)) // $!.^name, " ", (try $b.read-uint16(16)) // $!.^name, " ", (try $b.read-uint8(-1)) // $!.^name, " ", (try blob8.read-uint8(0)) // $!.^name, " ", (try $b.read-uint128(2)) // $!.^name, " ", buf8.new(1,2).read-uint16(0, BigEndian), " ", Buf.new(1,2).read-uint16(0, BigEndian), " ", Blob.new(1,2).read-uint16(0, BigEndian), " ", (try blob16.new(1,2).read-uint16(0, BigEndian)) // $!.^name, " ", (try $b.read-uint16(0, 2)) // $!.^name, " ", +NativeEndian, " ", +LittleEndian, " ", +BigEndian, " ", Endian.^name, " ", NativeEndian.^name, " ", (try $b.read-uint16(1.9, BigEndian)) // $!.^name, " ", (try $b.read-uint16("1", BigEndian)) // $!.^name, " ", (try $b.read-uint64(10)) // $!.^name, " ", $b.read-uint64(9, BigEndian), " ", (try $b.read-int128(2)) // $!.^name, " ", $b.read-int128(1, BigEndian), " ", $b.read-int8(0).^name, " ", $b.read-uint128(0).^name, " ", $b.read-int16(0, BigEndian).^name, " ", "a".encode.read-uint8(0), " ", (try Blob[int8].new(-1).read-uint8(0)) // $!.^name, " ", (try blob8.new(1,2,3,4).read-int32(0, Endian)) // $!.^name, " ", (try $b.read-uint16(0, "BigEndian")) // $!.^name, " ", (try Endian.enums.raku) // $!.^name
# rakudo 2026.08: 1 255 -1 258 513 4351 4351 -240 16909060 67305985 -15724786 72623859790382856 578437695752307201 -67537441387705590 1339673755198158349044581307228491536 21345817372864405881847059188222722561 -1245845896672164287427841985326349566 18446744073709551615 -1 340282366920938463463374607431768211455 -1 Int Int True LittleEndian X::AdHoc X::AdHoc X::AdHoc X::Parameter::InvalidConcreteness X::AdHoc 258 258 258 256 X::TypeCheck::Binding::Parameter 0 1 2 Endian Endian X::AdHoc X::AdHoc X::AdHoc 723685415333073151 X::AdHoc 2674114409790073892038207218725622015 Int Int Int 97 255 X::AdHoc X::TypeCheck::Binding::Parameter Map.new((:BigEndian(2),:LittleEndian(1),:NativeEndian(0)))
```
rakupp 4.0.1-84: differs — the refusals are `X::OutOfRange` and `X::Method::NotFound` instead of `X::AdHoc`/`X::Parameter::InvalidConcreteness`, an Int, Str or the type object is accepted as the Endian (513, 258), a Rat or Str offset is accepted (515), and `Endian.enums` is missing.

### BB-30  read-num32 and read-num64                                  D:yes R:yes V:spec
IEEE single/double at a byte offset with the same Endian rules; a
num32 is returned widened to `Num` (`3.1415927410125732` for the
single-precision pi bytes), `Inf`, `NaN`, `-Inf`, `-0` come back as
such. A buffer too short for the width, an offset past the end or
negative throw `X::AdHoc`; the type object is
`X::Parameter::InvalidConcreteness`; a 16-bit buffer reads its storage
bytes.
```
say buf8.new(0,0,128,63).read-num32(0, LittleEndian), " ", buf8.new(63,128,0,0).read-num32(0, BigEndian), " ", buf8.new(0,0,0,0,0,0,240,63).read-num64(0, LittleEndian), " ", buf8.new(63,240,0,0,0,0,0,0).read-num64(0, BigEndian), " ", buf8.new(0xDB,0x0F,0x49,0x40).read-num32(0, LittleEndian), " ", buf8.new(0,0,0,0).read-num32(0), " ", buf8.new(0,0,128,127).read-num32(0, LittleEndian), " ", buf8.new(0,0,192,127).read-num32(0, LittleEndian), " ", buf8.new(0,0,128,255).read-num32(0, LittleEndian), " ", buf8.new(0,0,128,63).read-num32(0).^name, " ", (try buf8.new(0,0,128).read-num32(0)) // $!.^name, " ", (try buf8.new(0,0,128,63).read-num32(1)) // $!.^name, " ", (try buf8.new(0,0,128,63).read-num32(-1)) // $!.^name, " ", (try buf8.read-num32(0)) // $!.^name, " ", buf8.new(1,0,0,0,0,0,128,63).read-num32(4, LittleEndian), " ", buf8.new(0,0,128,63).read-num32(0, LittleEndian) == 1e0, " ", buf8.new(0,0,0,0,0,0,0,0).read-num64(0), " ", buf8.new(0,0,0,0,0,0,0,128).read-num64(0, LittleEndian), " ", (buf8.new(0,0,128,63).read-num32(0, LittleEndian) == buf8.new(0,0,128,63).read-num32(0, NativeEndian)), " ", buf8.new(0,0,0,0,0,0,0,128).read-num64(0, LittleEndian).raku, " ", buf8.new(0,0,192,127).read-num32(0, LittleEndian).raku, " ", buf8.new(0x40,0x09,0x21,0xFB,0x54,0x44,0x2D,0x18).read-num64(0, BigEndian), " ", (try buf8.new(0,0,128,63).read-num64(0)) // $!.^name, " ", buf8.new(0,0,128,63).read-num32(0, LittleEndian).raku, " ", (try blob16.new(0, 0x3F80).read-num32(0)) // $!.^name
# rakudo 2026.08: 1 1 1 1 3.1415927410125732 0 Inf NaN -Inf Num X::AdHoc X::AdHoc X::AdHoc X::Parameter::InvalidConcreteness 1 True 0 -0 True -0e0 NaN 3.141592653589793 X::AdHoc 1e0 1
```
rakupp 4.0.1-84: differs — the refusals are `X::OutOfRange` and `X::Method::NotFound`; every value matches.

### BB-31  read-ubits and read-bits                                   D:partial R:partial V:bug
`read-ubits($pos, $bits)` reads `$bits` bits starting at BIT `$pos`,
most significant bit of byte 0 first, as an unsigned Int of any width
(65 bits and more work); `read-bits` interprets the same bits in two's
complement. A negative position, a position at or past `elems*8`, a
run that would pass the end, a Rat position throw `X::AdHoc`; a Rat bit
count is `X::TypeCheck::Binding::Parameter`; the type object is
`X::Parameter::InvalidConcreteness`. `read-ubits(p, 0)` is 0 and a
negative bit count reads 0. THE BUGS: `read-bits(p, 0)` is -1; and on a
16-, 32- or 64-bit buffer the methods index ELEMENTS as if they were
bytes without masking (`blob16.new(0x1234).read-ubits(0, 8)` is 4660,
not 52).
```
my $b = blob8.new(0x12, 0x34, 0x56); say $b.read-ubits(0, 8), " ", $b.read-ubits(4, 8), " ", $b.read-ubits(0, 4), " ", $b.read-ubits(4, 4), " ", $b.read-ubits(0, 16), " ", $b.read-ubits(3, 13), " ", $b.read-ubits(0, 24), " ", $b.read-ubits(23, 1), " ", $b.read-ubits(0, 1), " ", $b.read-bits(0, 4), " ", $b.read-bits(3, 5), " ", $b.read-bits(0, 8), " ", blob8.new(255).read-bits(0, 8), " ", blob8.new(255).read-ubits(0, 8), " ", blob8.new(0x80).read-bits(0, 1), " ", blob8.new(0x80).read-ubits(0, 1), " ", $b.read-ubits(0, 24).^name, " ", (try $b.read-ubits(0, 25)) // $!.^name, " ", (try $b.read-ubits(24, 1)) // $!.^name, " ", (try $b.read-ubits(-1, 1)) // $!.^name, " ", (try $b.read-ubits(0, 0)) // $!.^name, " ", (try $b.read-bits(0, 0)) // $!.^name, " ", (try blob8.read-ubits(0, 1)) // $!.^name, " ", buf8.new(0x12, 0x34).read-ubits(4, 8), " ", blob8.new(1..9).read-ubits(0, 65), " ", blob8.new(255 xx 9).read-bits(0, 65), " ", (try blob16.new(0x1234).read-ubits(0, 8)) // $!.^name, " ", $b.read-ubits(4, 8).base(16), " ", $b.read-ubits(20, 4), " ", $b.read-bits(0, 24), " ", blob8.new(0xFF, 0xFF, 0xFF).read-bits(0, 24), " ", blob8.new(0xFF, 0xFF, 0xFF).read-ubits(0, 24), " ", (try $b.read-ubits(0, -1)) // $!.^name, " ", (try $b.read-ubits(1.5, 4)) // $!.^name, " ", $b.read-bits(0, 4).^name, " ", (try $b.read-ubits(0, 1.5)) // $!.^name, " ", Blob.new(0x80).read-bits(0, 8), " ", "a".encode.read-ubits(1, 7)
# rakudo 2026.08: 18 35 1 2 4660 4660 1193046 0 0 1 -14 18 -1 255 -1 1 Int X::AdHoc X::AdHoc X::AdHoc 0 -1 X::Parameter::InvalidConcreteness 35 145247719580765712 -1 4660 23 6 1193046 -1 16777215 0 X::AdHoc Int X::TypeCheck::Binding::Parameter -128 97
```
rakupp 4.0.1-84: differs — every refusal is `X::OutOfRange` (including the zero and negative bit counts, which Rakudo answers), the type object is `X::Method::NotFound`, a Rat position or count is accepted, and the 16-bit buffer reads its bytes (52) — the last is the behaviour to keep.

### BB-32  write-int8 … write-int128, write-num32/64                   D:yes R:yes V:spec
`write-{int,uint}N($offset, $value, $endian = NativeEndian)` and
`write-num32/64` write at a BYTE offset, GROW the buffer with zeros as
needed, and return the buffer; called on the `buf8` type object they
return a new `Buf[uint8]` (on `Buf` a plain `Buf`). The value is
narrowed to the width (`write-uint8(0, 256)` writes 0, `write-int8(0,
200)` the byte 200, `write-uint16(0, 70000)` the low 16 bits,
`write-uint32(0, -1)` four 255s, `write-int32(0, 2**32)` zeros); the
64- and 128-bit forms take an `Int:D`/`UInt:D` (a negative into
`write-uint64` is `X::TypeCheck::Binding::Parameter`, a value wider
than 64 bits `X::AdHoc`). A negative offset, a Rat/Str offset or value,
`Nil`, an Int where a `num32`/`num64` is wanted (`write-num32(0, 1)`, a
Rat too) and an Int where an `Endian` is wanted throw `X::AdHoc`; a Blob
has no `write-*` (`X::Method::NotFound`). On a 16-bit buffer the bytes
land one per ELEMENT (`buf16.new.write-uint16(0, 1)` is `(1, 0)`).
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; say E({ buf8.new.write-uint8(0, 255).raku }), " ", E({ buf8.new.write-uint8(0, 256).raku }), " ", E({ buf8.new.write-int8(0, 200).raku }), " ", E({ buf8.new.write-int8(0, -1).raku }), " ", E({ buf8.new.write-uint16(0, 0x1234, BigEndian).raku }), " ", E({ buf8.new.write-uint16(0, 0x1234, LittleEndian).raku }), " ", E({ buf8.new.write-uint16(3, 1, BigEndian).raku }), " ", E({ buf8.new.write-int16(0, -2, BigEndian).raku }), " ", E({ buf8.new.write-uint32(0, 0x01020304, BigEndian).raku }), " ", E({ buf8.new.write-int32(0, -1).raku }), " ", E({ buf8.new.write-uint64(0, 2**64 - 1).raku }), " ", E({ buf8.new.write-int64(0, -1, BigEndian).raku }), " ", E({ buf8.new.write-int64(0, 2**62, BigEndian).raku }), " ", E({ buf8.new.write-uint128(0, 2**64 + 1, BigEndian).raku }), " ", E({ buf8.new.write-int128(0, -1).elems }), " ", E({ buf8.new.write-int128(0, -2, LittleEndian).list.head(2).raku }), " ", E({ buf8.new(9,9,9).write-uint8(1, 1).raku }), " ", E({ buf8.new(9,9,9).write-uint16(2, 1, BigEndian).raku }), " ", E({ buf8.write-uint16(0, 258, BigEndian).raku }), " ", E({ buf8.write-uint8(2, 1).raku }), " ", E({ Buf.new.write-uint8(0, 1).raku }), " ", E({ Buf.write-uint8(0, 1).raku }), " ", E({ buf8.new.write-uint8(-1, 1) }), " ", E({ buf8.write-uint8(-1, 1) }), " ", E({ blob8.new(1).write-uint8(0, 1) }), " ", E({ buf16.new.write-uint16(0, 1).raku }), " ", E({ buf8.new.write-uint8(0, "x") }), " ", E({ buf8.new.write-uint8(0, 1.5).raku }), " ", E({ buf8.new.write-uint16(0, 70000, BigEndian).raku }), " ", E({ buf8.new.write-uint64(0, -1) }), " ", E({ buf8.new.write-uint64(0, 2**64).raku }), " ", E({ buf8.new.write-int64(0, 2**64).raku }), " ", E({ my $b = buf8.new; $b.write-uint8(0, 1) === $b }), " ", E({ buf8.new.write-uint8(0, 1, 5) }), " ", E({ buf8.new.write-num32(0, 1e0, LittleEndian).raku }), " ", E({ buf8.new.write-num64(0, 1e0, BigEndian).raku }), " ", E({ buf8.new.write-num32(2, 1e0, BigEndian).raku }), " ", E({ buf8.new.write-num32(0, 1).raku }), " ", E({ buf8.new.write-num32(0, "1e0") }), " ", E({ buf8.write-num64(0, -0e0).raku }), " ", E({ buf8.new.write-num32(0, 1/3, LittleEndian).read-num32(0, LittleEndian) }), " ", E({ buf8.new.write-num64(0, 1/3, LittleEndian).read-num64(0, LittleEndian) == 1/3 }), " ", E({ buf8.new.write-uint8("0", 1).raku }), " ", E({ buf8.new.write-uint8(0.5, 1).raku }), " ", E({ buf8.new.write-int8(0, -129).raku }), " ", E({ buf8.new.write-int8(0, 128).raku }), " ", E({ buf8.new.write-uint32(0, -1) }), " ", E({ buf8.new.write-int32(0, 2**32).raku }), " ", E({ buf8.new.write-uint8(0, Nil) }), " ", E({ buf8.new.write-uint16(0, 1).elems }), " ", E({ buf8.new.write-num32(0, Inf, BigEndian).raku }), " ", E({ buf8.new.write-num32(0, NaN, BigEndian).read-num32(0, BigEndian).raku }), " ", E({ buf8.new.write-int8(1, 1).raku })
# rakudo 2026.08: ok:"Buf[uint8].new(255)" ok:"Buf[uint8].new(0)" ok:"Buf[uint8].new(200)" ok:"Buf[uint8].new(255)" ok:"Buf[uint8].new(18,52)" ok:"Buf[uint8].new(52,18)" ok:"Buf[uint8].new(0,0,0,0,1)" ok:"Buf[uint8].new(255,254)" ok:"Buf[uint8].new(1,2,3,4)" ok:"Buf[uint8].new(255,255,255,255)" ok:"Buf[uint8].new(255,255,255,255,255,255,255,255)" ok:"Buf[uint8].new(255,255,255,255,255,255,255,255)" ok:"Buf[uint8].new(64,0,0,0,0,0,0,0)" ok:"Buf[uint8].new(0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1)" ok:16 ok:"(254, 255).Seq" ok:"Buf[uint8].new(9,1,9)" ok:"Buf[uint8].new(9,9,0,1)" ok:"Buf[uint8].new(1,2)" ok:"Buf[uint8].new(0,0,1)" ok:"Buf.new(1)" ok:"Buf.new(1)" T:X::AdHoc T:X::AdHoc T:X::Method::NotFound ok:"Buf[uint16].new(1,0)" T:X::AdHoc T:X::AdHoc ok:"Buf[uint8].new(17,112)" T:X::TypeCheck::Binding::Parameter T:X::AdHoc T:X::AdHoc ok:Bool::True T:X::AdHoc ok:"Buf[uint8].new(0,0,128,63)" ok:"Buf[uint8].new(63,240,0,0,0,0,0,0)" ok:"Buf[uint8].new(0,0,63,128,0,0)" T:X::AdHoc T:X::AdHoc ok:"Buf[uint8].new(0,0,0,0,0,0,0,128)" T:X::AdHoc T:X::AdHoc T:X::AdHoc T:X::AdHoc ok:"Buf[uint8].new(127)" ok:"Buf[uint8].new(128)" ok:Buf[uint8].new(255,255,255,255) ok:"Buf[uint8].new(0,0,0,0)" T:X::AdHoc ok:2 ok:"Buf[uint8].new(127,128,0,0)" ok:"NaN" ok:"Buf[uint8].new(0,1)"
```
rakupp 4.0.1-84: differs — the type-object calls return a plain `Buf` (or `X::Method::NotFound` for `Buf.write-uint8`), a Blob accepts the writes, a negative offset is `X::OutOfRange`, and every other refusal is accepted: a Str/Rat/Nil value or offset, `-1` into `write-uint64`, `2**64`, an Int/Rat/Str into `write-num32`, and an Int as the Endian.

### BB-33  write-ubits and write-bits                                 D:partial R:partial V:bug
`write-ubits($pos, $bits, $value)` writes the low `$bits` bits of the
value at BIT `$pos` (most significant first), growing the buffer;
`write-bits` masks a signed value the same way (`write-bits(0, 4, -1)`
sets four bits). Both return the buffer, or a new `Buf[uint8]` from the
type object; a zero bit count writes nothing. A negative position throws
`X::AdHoc`; a negative value to `write-ubits`, a Str value or a Rat bit
count is `X::TypeCheck::Binding::Parameter`; a Rat position `X::AdHoc`;
a Blob has no `write-ubits`. THE BUG: when the run ends inside a byte,
the bits of that byte to the RIGHT of the run are not preserved:
`buf8.new(0xFF, 0xFF).write-ubits(4, 8, 0)` gives `(240, 8)` and
`buf8.new(0xFF).write-ubits(2, 4, 0)` gives `194`, where `(240, 15)`
and `195` are correct; do not imitate.
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; say E({ buf8.new.write-ubits(4, 8, 0xAB).raku }), " ", E({ buf8.new.write-ubits(0, 8, 0x1FF).raku }), " ", E({ buf8.new.write-ubits(0, 4, 0xF).raku }), " ", E({ buf8.new.write-ubits(0, 12, 0xABC).raku }), " ", E({ buf8.new.write-ubits(3, 13, 0x1FFF).raku }), " ", E({ buf8.new(0xFF, 0xFF).write-ubits(4, 8, 0).raku }), " ", E({ buf8.new.write-bits(0, 8, -1).raku }), " ", E({ buf8.new.write-bits(0, 4, -1).raku }), " ", E({ buf8.new.write-bits(0, 8, 300).raku }), " ", E({ buf8.new.write-ubits(0, 65, 2**65 - 1).elems }), " ", E({ buf8.new.write-ubits(0, 65, 2**65 - 1).read-ubits(0, 65) }), " ", E({ buf8.write-ubits(4, 8, 0xAB).raku }), " ", E({ buf8.write-bits(0, 8, -2).raku }), " ", E({ buf8.new.write-ubits(-1, 1, 1) }), " ", E({ buf8.new.write-ubits(0, 1, -1) }), " ", E({ buf8.new.write-ubits(0, 0, 0).raku }), " ", E({ blob8.new.write-ubits(0, 1, 1) }), " ", E({ my $b = buf8.new; $b.write-ubits(0, 1, 1) === $b }), " ", E({ buf8.new.write-ubits(0, 8, "x") }), " ", E({ buf8.new.write-ubits(16, 8, 1).raku }), " ", E({ buf8.new(1,2,3).write-bits(8, 8, -1).raku }), " ", E({ buf8.new.write-ubits(0, 8, 2**70).raku }), " ", E({ buf8.new.write-bits(0, 8, 2**70).raku }), " ", E({ buf8.new(0xFF).write-ubits(2, 4, 0).raku }), " ", E({ buf8.new.write-ubits(1, 8, 0xFF).raku }), " ", E({ buf8.new.write-ubits(0, 8, 1).write-ubits(8, 8, 2).raku }), " ", E({ buf8.new.write-ubits(0.5, 4, 1) }), " ", E({ buf8.new.write-bits(0, 4, 8).raku }), " ", E({ buf8.new.write-bits(0, 4, 8).read-bits(0, 4) }), " ", E({ buf8.new.write-ubits(0, 1, 1).raku }), " ", E({ buf8.new.write-ubits(7, 1, 1).raku }), " ", E({ buf8.new.write-ubits(0, 1.5, 1).raku })
# rakudo 2026.08: ok:"Buf[uint8].new(10,176)" ok:"Buf[uint8].new(255)" ok:"Buf[uint8].new(240)" ok:"Buf[uint8].new(171,192)" ok:"Buf[uint8].new(31,255)" ok:"Buf[uint8].new(240,8)" ok:"Buf[uint8].new(255)" ok:"Buf[uint8].new(240)" ok:"Buf[uint8].new(44)" ok:9 ok:36893488147419103231 ok:"Buf[uint8].new(10,176)" ok:"Buf[uint8].new(254)" T:X::AdHoc T:X::TypeCheck::Binding::Parameter ok:"Buf[uint8].new()" T:X::Method::NotFound ok:Bool::True T:X::TypeCheck::Binding::Parameter ok:"Buf[uint8].new(0,0,1)" ok:"Buf[uint8].new(1,255,3)" ok:"Buf[uint8].new(0)" ok:"Buf[uint8].new(0)" ok:"Buf[uint8].new(194)" ok:"Buf[uint8].new(127,128)" ok:"Buf[uint8].new(1,2)" T:X::AdHoc ok:"Buf[uint8].new(128)" ok:-8 ok:"Buf[uint8].new(128)" ok:"Buf[uint8].new(1)" T:X::TypeCheck::Binding::Parameter
```
rakupp 4.0.1-84: differs — the neighbouring bits ARE preserved (`(240, 15)`, `195`: the right answers), the type-object form returns a plain `Buf`, a Blob accepts the write, a negative position or a zero bit count is `X::OutOfRange`, and a negative value, Str value, Rat position or Rat count are accepted.

## H. Odds and ends

### BB-34  pack and unpack are gated                                  D:yes R:no V:spec
Without `use experimental :pack`, `.unpack` (Str or list template) and
the `pack` sub throw `X::Experimental` (`.feature` "the 'unpack' method"
or the sub); with the pragma they work, an unknown directive is
`X::Buf::Pack` (`.directive`), and there is no `unpack` sub. The
directive semantics are experimental and not recorded.
```
say (try Blob.new(1,2).unpack("C*")) // $!.^name ~ ":" ~ ((try $!.feature) // "-") ~ ":" ~ ((try $!.use) // "-"), " ", (try EVAL 'pack("C*", 1, 2).raku') // $!.^name, " ", (try EVAL 'use experimental :pack; pack("C*", 1, 2).raku') // $!.^name, " ", (try EVAL 'use experimental :pack; Blob.new(1,2).unpack("C*").raku') // $!.^name, " ", (try EVAL 'use experimental :pack; pack("n", 258).list.raku') // $!.^name, " ", (try EVAL 'use experimental :pack; pack("A3", "hi").list.raku') // $!.^name, " ", (try EVAL 'use experimental :pack; Blob.new(1,2,3,4).unpack("N").raku') // $!.^name, " ", (try EVAL 'use experimental :pack; pack("q", 1)') // $!.^name, " ", (try EVAL 'use experimental :pack; unpack(Blob.new(1), "C").raku') // $!.^name, " ", (try EVAL 'Blob.new(1).unpack(["C"])') // $!.^name, " ", (try EVAL 'use experimental :pack; pack("C*", 1, 2).^name') // $!.^name
# rakudo 2026.08: X::Experimental:the 'unpack' method:- X::Experimental Buf.new(1) (1, 2) (1, 2) (104, 105, 32) 16909060 X::Buf::Pack X::Undeclared::Symbols X::Experimental Buf
```
rakupp 4.0.1-84: differs — no gate (`unpack`/`pack` work without the pragma), `pack("C*", 1, 2)` packs both values where Rakudo packs one, and `pack("q", 1)` is accepted.

### BB-35  The type objects                                           D:no R:partial V:spec
`Blob` has `.elems` 1, gist `(Blob)`, raku `Blob`, is False and
undefined, `.list` is `(Blob,)`, `.Str` `""` and `+Blob` 0 (each with
the usual warning), `.encoding` `Any`, `.of` `uint8`, `.WHICH` a
`ValueObjAt`. `.decode`, `.subbuf`, `.sum`, `Buf.push`, `.pop`,
`.splice`, `.subbuf-rw` are `X::Multi::NoMatch`; `.reverse`, `.bytes`,
`.join`, `.Capture`, `.read-*` are `X::Parameter::InvalidConcreteness`;
`.write-*` `X::Method::NotFound`. `Blob ~~ Blob`, `Buf ~~ Blob`,
`~~ Positional`, `~~ Stringy` are True, `Blob ~~ Buf` and `5 ~~ Blob`
False. `.^roles` of `Blob` is `Positional[T]`, `Stringy`; an instance
adds the role itself and an implementation half (`UnsignedBlob[uint8]`
— measured, not a target).
```
say Blob.elems, " ", Blob.gist, " ", Blob.raku, " ", Blob.Bool, " ", Blob.list.raku, " ", (quietly Blob.Str).raku, " ", (try Blob.decode) // $!.^name, " ", (try Blob.new.decode.raku), " ", (try Blob.encoding.raku) // $!.^name, " ", Blob.of.^name, " ", Buf.of.^name, " ", Blob[int16].of.^name, " ", Blob.^name, " ", Buf.^name, " ", (try Blob.subbuf(0)) // $!.^name, " ", (try Blob.reverse) // $!.^name, " ", (try Blob.bytes) // $!.^name, " ", (try Buf.push(1)) // $!.^name, " ", (try Blob.read-uint8(0)) // $!.^name, " ", (Blob ~~ Blob), " ", (Blob ~~ Buf), " ", (Buf ~~ Blob), " ", (5 ~~ Blob), " ", (Blob ~~ Positional), " ", (Blob ~~ Stringy), " ", Blob.WHAT.gist, " ", Blob.new.HOW.^name, " ", Blob.new.WHAT.gist, " ", blob8.new.WHAT.gist, " ", Blob.new.^mro.map(*.^name).raku, " ", utf8.^mro.map(*.^name).raku, " ", (try Blob.^roles.map(*.^name).raku) // $!.^name, " ", (try Blob.new.^roles.map(*.^name).raku) // $!.^name, " ", (try utf8.^roles.map(*.^name).raku) // $!.^name, " ", (Blob.allocate(2) ~~ Blob), " ", Blob.new.^name, " ", (Blob.new.WHAT === Blob), " ", (blob8.new.WHAT === blob8), " ", (Blob.new(1).WHAT === Blob.new(2).WHAT), " ", (quietly (try Blob.Numeric) // $!.^name), " ", (quietly +Blob), " ", (try Blob.list.elems), " ", (quietly (try Blob.join(",")) // $!.^name), " ", (quietly (try Blob.sum) // $!.^name), " ", (try Blob.Capture) // $!.^name, " ", (try Buf.pop) // $!.^name, " ", (try Buf.splice) // $!.^name, " ", (try Buf.subbuf-rw) // $!.^name, " ", Blob.WHICH.^name, " ", (Blob === Blob), " ", (Blob === Blob[uint8]), " ", Blob.new.WHICH.^name, " ", (try Buf.allocate(2, 1).^name) // $!.^name, " ", (try Blob.write-uint8(0, 1)) // $!.^name, " ", (try Blob.read-ubits(0, 1)) // $!.^name
# rakudo 2026.08: 1 (Blob) Blob False (Blob,) "" X::Multi::NoMatch "" Any uint8 uint8 int16 Blob Buf X::Multi::NoMatch X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness X::Multi::NoMatch X::Parameter::InvalidConcreteness True False True False True True (Blob) Perl6::Metamodel::ClassHOW (Blob) (Blob[uint8]) ("Blob", "Any", "Mu").Seq ("utf8", "Any", "Mu").Seq ("Positional[T]", "Stringy").Seq ("Blob", "Positional[T]", "Stringy", "UnsignedBlob[uint8]").Seq ("Blob[uint8]", "UnsignedBlob[uint8]", "Positional[uint8]", "Stringy", "UnsignedBlob[uint8]").Seq True Blob False False True 0 0 1 X::Parameter::InvalidConcreteness X::Multi::NoMatch X::Parameter::InvalidConcreteness X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch ValueObjAt True False ValueObjAt Buf X::Method::NotFound X::Parameter::InvalidConcreteness
```
rakupp 4.0.1-84: differs — the refusals are `X::Method::NotFound` throughout (`.reverse` answers `((Blob))`, `.join` `""`, `.Capture` `\()`), `.encoding` and `.^roles` are missing, `blob8.new.WHAT.gist` is `(Blob)`, the MRO includes `Cool`, and `Blob.new.WHAT === Blob` is True.

### BB-36  gist truncation and long buffers                           D:partial R:partial V:spec
`.gist` shows at most 200 hex digits — 100 elements of an 8-bit buffer,
50 of a 16-bit, 25 of a 32-bit, 12 of a 64-bit (`int`/`uint` count as
64-bit) — and then ` ...` before the `>`; nothing else is elided.
`.raku` is always complete. A thousand elements build fine (values
wrap).
```
say Blob.new(1..100).gist.words.elems, " ", Blob.new(1..101).gist.words.elems, " ", Blob.new(1..101).gist.ends-with("...>"), " ", Blob.new(1..100).gist.ends-with("...>"), " ", Blob.new(1..101).gist.words.tail, " ", blob16.new(1..60).gist.words.elems, " ", blob16.new(1..50).gist.ends-with("...>"), " ", blob32.new(1..30).gist.words.elems, " ", blob64.new(1..20).gist.words.elems, " ", Blob[int].new(1..20).gist.words.elems, " ", Blob[int16].new(1..60).gist.words.elems, " ", Buf.new(1..1000).elems, " ", Buf.new(1..1000).raku.chars, " ", Buf.new(1..1000).raku.starts-with("Buf.new(1,2,3"), " ", Buf.new(1..1000)[255], " ", Buf.new(1..1000)[256], " ", Buf.new(1..300).list.tail, " ", Blob.new(1..101).gist.chars, " ", Blob.new(1..100).gist.chars, " ", Blob.new(1..3).gist.words.raku, " ", blob64.new(1).gist, " ", Blob[int8].new(-1, -128, 127).gist, " ", Blob[int64].new(-1).gist, " ", Blob[int32].new(-1).gist, " ", Blob[int].new(-1).gist, " ", Blob[uint].new(1).gist, " ", blob16.new(1..51).gist.words.tail, " ", Blob.new(1..102).gist.words.elems, " ", Blob[int8].new(1..101).gist.words.elems, " ", utf8.new(97 xx 101).gist.words.elems, " ", Buf.new(1..101).gist.words.elems, " ", Blob.new(1..1000).gist.chars, " ", Blob.new(1..100).raku.chars, " ", Blob.new(200 xx 3).gist, " ", Blob.new(0, 10, 15, 16).gist, " ", blob32.new(1..25).gist.ends-with("...>"), " ", blob32.new(1..26).gist.ends-with("...>"), " ", blob64.new(1..12).gist.ends-with("...>"), " ", blob64.new(1..13).gist.ends-with("...>")
# rakudo 2026.08: 100 101 True False ...> 51 False 26 13 13 51 1000 3570 True 0 1 44 312 308 ("Blob:0x<01", "02", "03>").Seq Blob[uint64]:0x<0000000000000001> Blob[int8]:0x<FF 80 7F> Blob[int64]:0x<FFFFFFFFFFFFFFFF> Blob[int32]:0x<FFFFFFFF> Blob[int]:0x<FFFFFFFFFFFFFFFF> Blob[uint]:0x<0000000000000001> ...> 101 101 101 101 312 301 Blob:0x<C8 C8 C8> Blob:0x<00 0A 0F 10> False True False True
```
rakupp 4.0.1-84: differs — `.gist` is never truncated (a 1000-element gist is 3,008 characters), and `Blob[int]`/`Blob[uint]` print as 8-bit.

### BB-37  Coercion, .Blob and .Buf                                   D:partial R:no V:spec
`Blob(x)`/`Buf(x)` on a value that is already one return it unchanged
(a Buf stays a Buf under `Blob(...)`); otherwise the coercion falls
through to `.new`, so `Blob([1,2])`, `Blob(1, 2)`, `utf8(Blob.new(97))`
work and `Blob("abc")` throws `X::TypeCheck`; `Blob(Any)` and
`Blob(Blob)` are coercion TYPES. `.Buf` on any Blob copies into a
`Buf` (plain for uint8, `Buf[T]` otherwise; `utf8.Buf` is a plain
`Buf`); `.Blob` exists on a Buf only (`Blob` for uint8, `Blob[T]`
otherwise; a Blob or utf8 has no `.Blob`); both are copies. A `Blob()`
parameter accepts a Buf as is and refuses a Str with `X::TypeCheck`; a
`Blob` parameter refuses a Str, a `Buf` one refuses a Blob, a `blob8`
one refuses a plain `Blob.new(1)` (`X::TypeCheck::Binding::Parameter`),
and a `Blob` one accepts a `blob16`. `Str(blob)` is `X::Buf::AsStr`,
`Int(blob)` the count, `List(blob)`/`Array(blob)` the elements.
```
sub E(&c) { my $o; { my \r = c(); $o = r ~~ Failure ?? do { r.so; "F:" ~ r.exception.^name } !! "ok:" ~ r.raku; CATCH { default { $o = "T:" ~ .^name } } }; $o }; my $s = "x"; say E({ Blob("abc") }), " ", E({ Buf("abc") }), " ", E({ Blob([1,2]) }), " ", E({ Blob(Buf.new(1,2)) }), " ", E({ Buf(Blob.new(1,2)) }), " ", E({ Blob(Blob.new(1)) }), " ", E({ Buf.new(1,2).Blob.raku }), " ", E({ Blob.new(1,2).Buf.raku }), " ", E({ buf8.new(1).Blob.^name }), " ", E({ blob8.new(1).Buf.^name }), " ", E({ Buf[int8].new(-1).Blob.^name }), " ", E({ Blob[int16].new(1).Buf.^name }), " ", E({ utf8.new(97).Buf.raku }), " ", E({ utf8.new(97).Blob.raku }), " ", E({ Blob.new(1).Blob.raku }), " ", E({ Buf.new(1).Buf.raku }), " ", E({ my $b = Buf.new(1); $b.Buf === $b }), " ", E({ my $b = Blob.new(1); $b.Blob === $b }), " ", E({ sub f(Blob() $b) { $b.raku }; f(Buf.new(3)) }), " ", E({ sub f(Blob() $b) { $b.raku }; f($s) }), " ", E({ sub f(Blob $b) { $b.raku }; f($s) }), " ", E({ sub f(Buf $b) { $b.raku }; f(Blob.new(1)) }), " ", E({ sub f(blob8 $b) { $b.raku }; f(Blob.new(1)) }), " ", E({ sub f(Blob $b) { $b.raku }; f(blob16.new(1)) }), " ", E({ Blob(1, 2) }), " ", E({ Blob.new(1,2).Buf.push(3).raku }), " ", E({ Str(Blob.new(97)) }), " ", E({ Int(Blob.new(97, 98)) }), " ", E({ Blob.new(1).List.raku }), " ", E({ List(Blob.new(1,2)).raku }), " ", E({ Buf(utf8.new(97)).raku }), " ", E({ Blob(utf8.new(97)).^name }), " ", E({ utf8(Blob.new(97)).raku }), " ", E({ blob8(Blob.new(97)).raku }), " ", E({ sub f(utf8() $b) { $b.raku }; f("a") }), " ", E({ Buf.new(1).Blob.WHICH.^name }), " ", E({ Blob.new(1).Buf.WHICH.^name }), " ", E({ Blob[int8].new(-1).Buf.list.raku }), " ", E({ Buf.new(1).Blob ~~ Buf }), " ", E({ Array(Blob.new(1,2)).raku }), " ", E({ Blob(Any) }), " ", E({ Blob(Blob) }), " ", E({ Buf(Buf.new(1)).^name }), " ", E({ Blob.new(1).Array.^name })
# rakudo 2026.08: T:X::TypeCheck T:X::TypeCheck ok:Blob.new(1,2) ok:Buf.new(1,2) ok:Buf.new(1,2) ok:Blob.new(1) ok:"Blob.new(1,2)" ok:"Buf.new(1,2)" ok:"Blob" ok:"Buf" ok:"Blob[int8]" ok:"Buf[int16]" ok:"Buf.new(97)" T:X::Method::NotFound T:X::Method::NotFound ok:"Buf.new(1)" ok:Bool::False T:X::Method::NotFound ok:"Buf.new(3)" T:X::TypeCheck T:X::TypeCheck::Binding::Parameter T:X::TypeCheck::Binding::Parameter T:X::TypeCheck::Binding::Parameter ok:"Blob[uint16].new(1)" ok:Blob.new(1,2) ok:"Buf.new(1,2,3)" T:X::Buf::AsStr ok:2 ok:"(1,)" ok:"(1, 2)" ok:"Buf.new(97)" ok:"utf8" ok:"utf8.new(97)" ok:"Blob[uint8].new(97)" T:X::TypeCheck ok:"ValueObjAt" ok:"ObjAt" ok:"(-1,)" ok:Bool::False ok:"[1, 2]" ok:Blob(Any) ok:Blob(Blob) ok:"Buf" ok:"Array"
```
rakupp 4.0.1-84: differs — `Blob(...)`, `Buf(...)`, `.Blob`, `.Buf`, `utf8(...)`, `blob8(...)` and `Blob(Any)` are all `X::Method::NotFound`, a `Blob()` parameter refuses a Str with `X::Coerce::Impossible`, a `Buf` parameter accepts a Blob and a `blob8` one a plain Blob, `Str(Blob.new(97))` is `"a"`, and `List(blob)`/`Array(blob)` wrap the whole buffer.

## Counts

| | items |
|---|---|
| total | 37 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 21 |
| Rakudo bugs (do not imitate) | 9 — BB-03 `.bytes` of `Blob[int]`, BB-04 `allocate` with an empty pattern hangs, BB-11 a bad element in a multi-value `push` grows the buffer before throwing, BB-12 a failed `splice` grows or corrupts the buffer and an infinite replacement hangs, BB-16 `~&`/`~|` between signed buffers of unequal length, BB-21 `subbuf("1", 1)` recurses, BB-24 `encode(42)` recurses, BB-31 `read-bits(p, 0)` and the 16/32/64-bit indexing, BB-33 `write-ubits` clobbers the bits right of the run |
| quirks (recorded, step two decides) | 8 — BB-01 `.WHAT === Blob` False and `Blob ~~ Cool` True on the type object only, BB-07 `cmp` orders by count first, BB-15 `X` vs `Z` and `[+]` as the count, BB-18 `reduce`/`produce` see one item, BB-19 a Buf's element views are live references, BB-22 the `subbuf-rw` Proxy reads back the old stretch, BB-26 `:replacement` on decode only under `:strict` and an empty buffer ignores the name, BB-29 `read-*` on a 16/32/64-bit buffer reads storage bytes |
| rakupp 4.0.1-84 differs | 37 |
| rakupp 4.0.1-84 matches | 0 |

Recurring rakupp gaps, for step two: the type family is flat (`blob8`
is not `Blob[uint8]`, `utf8` is a plain `Blob[uint8]` with no Str side,
`Blob[Int]` is accepted, the MRO holds `Cool`); a Blob is not a value
type (`WHICH`, `===`, `eqv`, `~` result); refusals are the wrong kind
in every family — `X::Method::NotFound` where Rakudo has
`X::Multi::NoMatch`/`X::Parameter::InvalidConcreteness`, `X::OutOfRange`
where Rakudo throws `X::AdHoc`, a throw where Rakudo returns a Failure
(`pop`, a negative-index write, `allocate`), and silent acceptance where
Rakudo refuses (Str/Rat/Nil elements and offsets, cross-type `cmp`,
non-Endian endians, lazy lists, unknown encoding names on decode); a
Buf's elements are values, not references, and `my $d = $c` copies; the
Positional surface is thin (`.sum`/`.min`/`.max`/`.join`/`.reverse`
wrong, `.is-lazy`/`.^roles` missing, adverbs and `AT-POS(i) = v`
missing, out-of-range reads answer `Any`); the encoding layer ignores
the encoding name (every `encode` is utf8, windows-1251 decodes as utf8,
`:strict`/`:replacement` inert, utf16le/be produce code units, the
registry has no alternative names or replacement encoders); `is buf8`
traits are ignored; `.Buf`/`.Blob`/coercions are missing; `reallocate(-1)`
crashes the process. Where Rakudo is wrong (BB-16, BB-31 element
indexing, BB-33) rakupp already does the right thing and should keep it.

## Method (how this sheet was produced)

As for [Supply.md](Supply.md) and [IO.md](IO.md): one probe line per
item (two where a hang needed its own line), run through `alarm 10` in a
fresh sandbox per engine, outputs joined with `|`, assembled into a TSV
of probe, Rakudo and rakupp columns. Six probe rounds. Traps met: a
Failure returned by a method is classified by the `E` helper (throw vs
Failure) because `try EXPR` turns a returned Failure into `$!` and Nil,
hiding the distinction; `fail` inside a bare block passed to `E` throws
instead, so the helper is only used on method results; a hang leaves an
empty output and a 142 exit status, so every empty Rakudo output was
timed by hand before being recorded as a hang; `Blob[int8, int8]` and
`f("x")` against a `Blob` parameter are compile-time errors that kill a
whole line; `Blob.WHICH` on the type object and `.Set.raku`/`.Bag.raku`
are per-process and were dropped; `Buf.new(255).decode("utf8",
:replacement("?"))` throws, so every decode with a replacement went into
a `try`; `X~` and `Z` treat a buffer differently and the difference only
showed once each was on its own; three Rakudo outputs contained a
decoded non-ASCII character and were changed to `.ord`; a `say` prints
nothing when a later argument dies, so a rakupp gap late in a line hides
every measurement before it — the wrapping in `try` was pushed to the
last unwrapped field for that reason. D flags from `doc/Type/Blob.rakudoc`,
`Buf.rakudoc`, `utf8.rakudoc`, `Encoding.rakudoc`, `Encoding/Registry.rakudoc`,
`Str.rakudoc` (encode) and `Language/{operators,unicode,experimental}.rakudoc`;
R flags from `S03-operators/buf.t`, `S32-container/buf.t`,
`S03-buf/*.t`, `S32-str/encode.t`, `utf8-c8.t`,
`windows-1251-windows-1252-encode-decode.t`, `S32-encoding/*.t`,
`S02-types/{WHICH,is-type,capture,sigils-and-types}.t` and
`S09-subscript/slice.t`.
