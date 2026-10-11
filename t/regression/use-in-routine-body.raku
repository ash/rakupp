# Regression: a `use` inside a routine body (2026-10-11).
#
# It loaded when the routine first RAN. Two bugs came of that:
#   - concurrent first calls collided in the loader: `sub t { use Foo; f() }`
#     run from eight `start` blocks died "Circular module loading detected
#     trying to precompile Foo" — the second thread found Foo in the set of
#     modules being loaded and took the race for a cycle (sometimes naming a
#     module Foo itself uses). The loader is now one critical section.
#   - a missing module in a routine nobody called went unreported; Rakudo
#     refuses it at compile time. Such a `use` now loads when the statement
#     declaring the routine is reached, importing nothing until the routine runs.

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eq $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

my $dir = $*TMPDIR.add("rakupp-use-in-routine-$*PID");
mkdir $dir;
$dir.add('UirFoo.rakumod').spurt: "unit module UirFoo;\nsub f is export \{ 'f' }\n";
$dir.add('UirBar.rakumod').spurt:
    "unit module UirBar;\nuse UirFoo;\nsay 'Bar loads';\nsub g is export \{ f() ~ 'g' }\n";

sub child($code) {
    my $p = run $*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err;
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).trim
}

# eight threads make the first call together, several times over
my $race = 'sub t { use UirBar; g() }; say await (^16).map({ start t() })';
for ^10 {
    check child($race), "Bar loads\n(" ~ ('fg' xx 16).join(' ') ~ ')', "concurrent first use, run $_";
}

# the module body runs once, where the routine is declared, not at the call
check child('say 1; sub t { use UirBar; g() }; say 2; say t(); say t()'),
      "1\nBar loads\n2\nfg\nfg", 'loaded at the declaration, once';

# …and imports nothing outside the routine
check child('sub t { use UirBar; g() }; say (try g()) // "not here"; say t()'),
      "Bar loads\nnot here\nfg", 'the import stays in the routine';

# a method and an anonymous sub count as routines too
check child('class C { method m { use UirBar; g() } }; say C.m'),
      "Bar loads\nfg", 'a use in a method';
check child('my &h = sub { use UirBar; g() }; say "decl"; say h()'),
      "Bar loads\ndecl\nfg", 'a use in an anonymous sub';

# a braced `module M { sub s is export }` loaded ahead of the routine still
# hands the routine its export (S10-packages/export.t) — and so does the
# `need` + `use` pair that ahead-of-time load amounts to
$dir.add('UirBraced.rakumod').spurt: "module UirBraced \{ sub b is export \{ 'b' } }\n";
check child('sub t { use UirBraced; b() }; say t(); say t()'), "b\nb", 'a braced module, in a routine';
check child('need UirBraced; use UirBraced; say b()'), "b", 'a braced module, need then use';

# a missing module in a routine never called is still an error
my $missing = child('sub t { use UirNoSuchModule; 1 }; say "reach" ~ "ed"');
check ($missing.contains('Could not find UirNoSuchModule') && !$missing.contains('reached')).Str, 'True',
      'a missing module in an uncalled routine';

# a real cycle is still one
$dir.add('UirCycA.rakumod').spurt: "unit module UirCycA;\nuse UirCycB;\n";
$dir.add('UirCycB.rakumod').spurt: "unit module UirCycB;\nsub h \{ use UirCycA; 1 }\n";
check child('use UirCycA; say "no"').contains('Circular module loading').Str, 'True',
      'a cycle through a routine body';

for $dir.dir { .unlink }
rmdir $dir;

if $fails { die "$fails case(s) failed" }
say 'PASS';
