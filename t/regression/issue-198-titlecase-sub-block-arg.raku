# Issue #198: a capitalized sub name followed by a block did not pass the
# block. `sub Foo(&bar) { bar }; Foo { say "bar" }` died with "Calling Foo()
# will never work with declared signature (&bar)", where `foo { … }` with a
# lowercase name calls foo with the block, in rakupp and in Rakudo alike. The
# parser read every capitalized bareword before a `{` as a type followed by a
# block of its own (`when Int { }`, `if $x ~~ Mu { }`), so `Foo` was called
# with nothing and the block ran as a bare block after it.
#
# A capitalized name now takes the listop path when the unit declares a sub of
# it, above or below, or a `use`d module does, and no type has the name. In a
# statement condition that path leaves the brace to the statement, as it does
# for a lowercase name.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

my @log;

# the issue's own program, with the `say` caught
sub Foo(&bar) { bar }; Foo { @log.push: "bar" }
check @log.List, ("bar",), "the issue's program";

# the block is the call's argument wherever a term may stand
check (Foo { 42 }), 42, 'Foo { … } as an expression';
my $x = Foo { 7 };
check $x, 7, 'Foo { … } assigned';
sub Plus(&b, $n) { b() + $n }
check (Plus { 1 }, 2), 3, 'a block and another argument';

# a hash-shaped block is a Hash argument, as it is to a lowercase listop
sub Count(*@a) { @a.elems }
check (Count { a => 1 }), 1, 'a hash-shaped block reaches a slurpy';
sub Kind($h) { $h.^name }
check (Kind { a => 1 }), 'Hash', '…and it is a Hash';

# a sub declared further down the file, and a lexical one
@log = ();
Later { @log.push: "later" };
sub Later(&b) { b() }
check @log.List, ("later",), 'a sub declared below the call';
check (Below { "below" }), "below", 'a multi declared below, without `sub`';
multi Below(&b) { b() }
{
    my sub Lex(&b) { b() }
    check (Lex { "lex" }), "lex", 'a lexical my sub';
}

# in an interpolated block, and in an EVAL
check "<{ Foo { 'interp' } }>", '<interp>', 'inside "{ … }"';
use MONKEY-SEE-NO-EVAL;
check EVAL('Foo { "ev" }'), "ev", 'inside an EVAL';

# inside a loop, a method, and a `when` block (a statement's own block)
@log = ();
for 1..2 { Foo { @log.push: $_ } }
check @log.List, (1, 2), 'inside a for loop';
class Holder { method m { Foo { "in method" } } }
check Holder.m, "in method", 'inside a method';
given 3 { when Int { check (Foo { "w" }), "w", 'inside a when block' } }

# multi subs, and the statement after a call
multi Many(&b) { b() }
multi Many(&b, $n) { b() x $n }
check (Many { "m" }), "m", 'a multi sub, one candidate';
check (Many { "m" }, 3), "mmm", 'a multi sub, the other';
@log = ();
Foo { @log.push: "a" }; @log.push: "after";
check @log.List, ("a", "after"), 'the statement after the call runs once';

# a TYPE before a brace is still the type, with the statement's block after it
my @seen;
for 42, "s", 1.5 -> $v {
    given $v {
        when Int { @seen.push: "Int" }
        when Str { @seen.push: "Str" }
        default  { @seen.push: "other" }
    }
}
check @seen.List, <Int Str other>, 'when Type { … } is still the type';
class Pt { }
my $p = Pt.new;
my $hit = False;
if $p ~~ Pt { $hit = True }
check $hit, True, 'if $x ~~ Class { … } is still the class';
$hit = False;
if $p ~~ Mu { $hit = True }
check $hit, True, 'if $x ~~ Mu { … } is still the type';

# a sub a `use`d module exports, cold and cached
my $dir = $*TMPDIR.add("issue198-{$*PID}");
$dir.mkdir;
$dir.add('I198Wrap.rakumod').spurt: q:to/END/;
    unit module I198Wrap;

    sub Wrap(&b) is export { "<" ~ b() ~ ">" }
    END
for <cold cached> -> $run {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', 'use I198Wrap; say Wrap { "in" }', :out, :err);
    my $err = $p.err.slurp(:close);
    check $p.out.slurp(:close).lines.List, ("<in>",),
        "an imported sub, $run run{$err ?? " ($err.lines.head())" !! ''}";
}
for $dir.dir { .d ?? run('rm', '-rf', ~$_) !! .unlink }
$dir.rmdir;

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
