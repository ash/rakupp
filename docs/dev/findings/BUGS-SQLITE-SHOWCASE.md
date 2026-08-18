# Bugs and divergences found while writing the SQLite showcase

Found 18 August 2026 while building [`showcase/sqlite/`](../../../showcase/sqlite)
— a SQLite client that reaches `libsqlite3` through NativeCall and drives the
terminal through `termios` — under `build/rakupp`, cross-checked against Rakudo
(v2026.06) and against the real `sqlite3` C shell.

This is the first showcase built on NativeCall, and it is a different crop from
the interpreter showcases: most of what follows is about the boundary between
Raku and C, or about I/O, rather than about parsing. Three of them corrupt
memory or lose data silently, which is worse than a parse error — nothing
reports anything, the program simply does the wrong thing.

Each entry has a minimal reproducer, expected (Rakudo) versus actual (rakupp),
and the workaround applied in `showcase/sqlite/sqlite.raku`, so a fix lets a
workaround retire. None of them blocked the showcase: it runs, its csv and
json output is byte-identical to the real `sqlite3` shell, and the whole
program is byte-identical under both engines (`compare.sh`).

---

## Silent corruption or data loss

### S1. `LEAVE` inside a *method* runs at its declaration, not at scope exit

```raku
sub in-sub()   { say "before"; LEAVE say "LEAVE ran"; say "after" }
class C { method in-method() { say "before"; LEAVE say "LEAVE ran"; say "after" } }
in-sub();        # both engines: before / after / LEAVE ran
C.in-method();   # rakudo:  before / after / LEAVE ran
                 # rakupp:  before / LEAVE ran / after      <-- fires immediately
```

The phaser runs the moment control reaches its declaration, so the rest of the
method body executes *after* the cleanup. In the binding this was
`LEAVE $st.finish` in `DB.query`: the statement was finalized before the first
row was read, and every column accessor then read freed memory — garbage
values (`6.9474407508111e-310`) and a segfault. A `LEAVE` in a plain `sub` is
correct, which is why this went unnoticed for so long.

- **Workaround:** `DB.query` finalizes by hand and rethrows through a captured
  variable instead of using the phaser.
- **Severity:** high. Silent use-after-free in any RAII-style method.

### S2. A user method named `throw` makes the invocant the thrown exception

```raku
class Holder { method throw(Str $why) { die X::AdHoc.new(payload => "real: $why") } }
my $e;
{ Holder.new.throw('bad table'); CATCH { default { $e = $_ } } }
say $e.^name;    # rakudo: X::AdHoc     rakupp: Holder
```

The `die` inside the method is discarded and the invocant arrives at `CATCH`
instead, so the real error is lost and the handler blows up on
`No such method 'message' for invocant of type 'Holder'`. Presumably
`$obj.throw` is being treated as "throw this object" the way `Exception.throw`
works, without checking that the invocant is an Exception.

- **Workaround:** the error-raising helper on `DB` is named `fail-with`.
- **Severity:** high. It swallows a real exception and reports a wrong one.

### S3. `$*OUT.write(Buf)` writes nothing at all

```raku
$*OUT.write(Buf.new(0x68, 0x69, 0x0a));   # rakudo: "hi\n"   rakupp: nothing
say "after";                              # both: "after"
```

No error, no output. A handle the program opens itself is fine —
`open('/dev/stdout', :w, :bin).write($buf)` writes the right bytes — so this is
specific to `$*OUT`. It matters for any binary output; here a blob column would
have vanished from csv output.

- **Workaround:** `write-bytes()` opens `/dev/stdout` in binary mode once and
  writes through that.
- **Severity:** high, because it is silent.

### S4. On a terminal in raw mode, every high-level read returns nothing

```raku
# with the tty in raw mode (cfmakeraw + tcsetattr):
$*IN.read(1)        # rakudo: Buf with 1 byte     rakupp: empty Buf, immediately
$*IN.getc           # rakudo: 'a'                 rakupp: Nil
$*IN.readchars(1)   # rakudo: 'a'                 rakupp: ''
read(2) via NativeCall                            # both: works
```

None of them block and none of them return data, so a keyboard loop spins on
empty input — the browser exited instantly, because `read-key` read its
"stdin is closed" fallback on the very first keystroke. The `read(2)` syscall
on the same descriptor works, so the terminal state is fine.

- **Workaround:** the browser reads keys through `read(2)` with NativeCall.
- **Severity:** high for interactive programs; blocks any TUI written in
  ordinary Raku.

---

## The C boundary

### C1. `Pointer` numification, truthiness and definedness all diverge

```raku
my $null = Pointer.new;
my $real = strdup('hello');
#                     rakudo                          rakupp
$null.defined     #   True                            False
?$null            #   False                           True
+$null            #   0                               2
+$real            #   the address                     2
"$real"           #   NativeCall::Types::Pointer<…>   "addr\t140…\nof\t"  (multi-line)
```

`+$pointer` is 2 for every pointer, so an address can never be read back, and
the two engines disagree about *both* standard NULL tests — in opposite
directions, so neither `if $p` nor `$p.defined` is portable on its own. The
interpolated form is not just differently formatted, it contains a tab and a
newline and looks like a dumped structure.

- **Workaround:** `sub null-ptr($p) { !$p.defined || !$p }`, which is correct on
  both engines; pointers are never interpolated.
- **Severity:** medium-high. The natural spelling of a NULL check silently
  passes a NULL pointer to C.

### C2. A NULL `char *` return becomes `""` instead of an undefined `Str`

```raku
sub getenv(Str --> Str) is native { * }
my $v = getenv('DEFINITELY_NOT_SET');
say $v.defined;      # rakudo: False    rakupp: True (an empty string)
```

The difference between "no value" and "empty value" is lost, which for a
database client is exactly the NULL-versus-`''` distinction.

- **Workaround:** values are fetched by storage class (`sqlite3_column_type`
  first), so the binding never relies on the return being undefined.
- **Severity:** medium.

---

## Parsing and semantics

### P1. `%{EXPR}` in a double-quoted string swallows the `%` and the block

```raku
my $n = 7;
say "[%{$n}s]";      # rakudo: [%7s]     rakupp: [s]
say "[%-{$n}s]";     # both:   [%-7s]
```

A `%` immediately before a block is treated as an interpolation and produces
nothing; with any character in between it is literal. This is how a width gets
built into a format string, so `.fmt("%{$w}s")` quietly formats everything to
the single letter `s`.

- **Workaround:** format strings are concatenated: `.fmt('%' ~ $w ~ 's')`.
- **Severity:** medium.

### P2. A nested `my sub` named `rule`, `token` or `regex` is silently never called

```raku
sub outer() {
    my sub rule($x)  { say "rule($x)"  }
    my sub token($x) { say "token($x)" }
    my sub plain($x) { say "plain($x)" }
    rule('a'); token('b'); plain('d');
}
outer();     # rakudo: rule(a) token(b) plain(d)     rakupp: plain(d)
```

The declaration is accepted and the call is accepted; nothing happens and
nothing is reported. Presumably the call parses as a declarator. Compare G1 in
[`BUGS-JS-SHOWCASE.md`](BUGS-JS-SHOWCASE.md), which was the same class of
problem for `grammar Q`.

- **Workaround:** the table renderer's helper is named `hrule`.
- **Severity:** medium, entirely because it is silent.

### P3. A sub whose name looks like a version literal cannot be called

```raku
sub v1() { say "sub v1 called" }
v1();     # rakudo: sub v1 called
          # rakupp: Cannot invoke non-Callable value of type Version
```

`v1` at the call site lexes as a `Version` literal even though a sub of that
name is in scope. Found by accident while naming test cases `v1()`…`v4()`.

- **Workaround:** none needed in the showcase.
- **Severity:** low.

### P4. `my @a = @rows[0]` flattens one level, and not consistently

```raku
my @rows = ((1, 'ada'), (2, 'grace'));
my @b = @rows[0];       # rakudo: 1 element (the inner list)   rakupp: 2 elements
```

Raku++ flattens the indexed element, Rakudo keeps it as one item. It is also
not consistent within Raku++: the same expression against an array that came
from an object attribute gave one nested element instead. Whichever way it is
meant to go, code that indexes a list of rows sees a different shape on the two
engines.

- **Workaround:** `(@rows[$i] // ()).list`, explicit on both engines.
- **Severity:** medium.

---

## Not bugs — noted so they are not re-filed

- **Lazy rows over a freed statement.** `(^$n).map({ … }).List` stays lazy under
  Rakudo, so rows reified after `sqlite3_finalize` read freed memory and
  segfault. Raku++ reifies eagerly and happened to survive. The showcase's
  `.eager` is the correct fix, not a workaround — this was our bug.
- **`sqlite3 -csv` truncates a blob at an embedded NUL**, because its csv writer
  formats through C strings. The showcase writes the whole blob, so that one
  case is checked for engine parity rather than against the C shell.
- **`sqlite3 -json` prints floats with `%!.20g`**, a shortest-round-trip
  extension of its own printf. The showcase follows the 15-digit form used
  everywhere else in SQLite; fixtures use binary-exact fractions so the two
  agree byte for byte.
