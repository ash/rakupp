# Regression: a file headed `unit module Foo;` did not publish its `our sub`s
# under the qualified name, so the program could not call its own
# `Foo::name()` — although an `our` VARIABLE of the same module published
# perfectly well:
#
#     unit module Quux;
#     our sub deep { "d" }
#     our $v = 9;
#     say $Quux::v;      # 9
#     say Quux::deep();  # "Undefined routine 'Quux::deep'"
#
# A named sub is HOISTED — hoistSubs runs its declaration at scope entry, which
# is before the `unit module Quux;` statement has executed and set the package
# prefix — so when the declaration published, there was no prefix to publish
# under. The module-LOADING path had always repaired this at the declaration's
# textual position, by which time the header has run; the MAINLINE path had
# not, and that is the whole of the difference.
#
# No Roast file exercises it (the suite's unit-module tests are all loaded
# modules, which took the repaired path), which is why the case lives here.
#
# The braced form is checked alongside it because the repair must not reach it:
# `module A { … }` publishes from inside its own namespace and always worked.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; note "FAIL: $desc\n  got:  {$got.raku}\n  want: {$want.raku}" }
}

# ---- 1. the braced form, which must keep behaving ------------------------
module A { our sub f { "Af" }; our $x = 1 }
ck A::f(),  "Af", 'a braced module publishes its `our sub` qualified';
ck $A::x,   1,    '…and its `our` variable';

module B { module C { our sub g { "BCg" } } }
ck B::C::g(), "BCg", 'nested braced modules publish through both levels';

# ---- 2. the unit form, in a process of its own ---------------------------
# It has to be the whole file, so it cannot be written inline here.
sub run-unit(@body) {
    my $f = $*TMPDIR.add("unit-our-sub-{$*PID}-{@body.elems}.raku");
    $f.spurt(@body.join("\n") ~ "\n");
    my $p = run($*EXECUTABLE, $f.absolute, :out, :err);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    $f.unlink;
    $err ?? "ERR: " ~ $err.lines[0] !! $out.lines.List
}

ck run-unit(['unit module Quux;',
             'our sub deep { "d" }',
             'our $v = 9;',
             'say $Quux::v;',
             'say Quux::deep();']),
   ('9', 'd'),
   'a unit module reaches its own `our sub` by qualified name';

ck run-unit(['unit module Quux;',
             'our sub deep { "d" }',
             'say &Quux::deep();',
             'say deep();']),
   ('d', 'd'),
   '…through the `&` spelling and the bare one alike';

ck run-unit(['unit module Quux;',
             'my sub hidden { "h" }',
             'say (Quux::hidden() // "undef");']),
   "ERR: Undefined routine 'Quux::hidden'",
   'a `my sub` is NOT published — only `our` is package-scoped';

ck run-unit(['unit module Deep::Down;',
             'our sub far { "f" }',
             'say Deep::Down::far();']),
   ('f',),
   'a multi-part unit module name publishes under the whole path';

# ---- 3. a LOADED unit module keeps its own repair ------------------------
{
    my $dir = $*TMPDIR.add("unit-our-sub-lib-{$*PID}");
    $dir.mkdir;
    $dir.add('UMod.rakumod').spurt(q:to/MOD/);
        unit module UMod;
        our sub pub is export { "UP" }
        our sub unexported { "UU" }
        MOD
    my $f = $*TMPDIR.add("unit-our-sub-use-{$*PID}.raku");
    $f.spurt(qq:to/USE/);
        use lib '{$dir.absolute}';
        use UMod;
        say pub();
        say UMod::pub();
        say UMod::unexported();
        USE
    my $p = run($*EXECUTABLE, $f.absolute, :out, :err);
    my $out = $p.out.slurp(:close); $p.err.slurp(:close);
    $f.unlink; $dir.add('UMod.rakumod').unlink; $dir.rmdir;
    ck $out.lines.List, ('UP', 'UP', 'UU'),
       'a LOADED unit module still publishes exactly once, exported or not';
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
