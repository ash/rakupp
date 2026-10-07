# Regression: a statement label cannot shadow a pseudo-package. Under
# `OUTER: for …`, the word in `next OUTER` is the OUTER pseudo-package, not the
# label, and no candidate of `next` takes it. On this snippet (Raku Koans,
# koans/07-control-flow/07-loop-control-and-phasers, found 2026-09-28):
#
#   OUTER: for 1..2 -> $i { for 1..2 -> $j { next OUTER if $j > $i; print "$i$j " } }; say "done"
#
#   rakudo 2026.09:   11 Cannot resolve caller next(OUTER:U); none of these signatures matches:
#                         ( --> Nil)
#                         (Label:D $x --> Nil)
#   rakupp 5.2.1:     11 21 22 done
#
# The same holds for last/redo and for MY OUR CORE SETTING UNIT OUTERS CALLER
# CALLERS DYNAMIC LEXICAL CLIENT; `next GLOBAL` is refused at compile time. A
# label named COMPILING, PROCESS, PARENT or a type name is found as before, and
# from 6.e `next OUTER` passes the package as the iteration's value.
#
# Fixed with it: the argument of `next`/`last`/`redo` was dropped, so
# `next(FOO)`, `next $label` and `last(FOO)` left the innermost loop, `redo $l if
# …` parsed as an unconditional `redo` (an endless loop), and `next $type`
# succeeded. Label had no .next/.last/.redo methods.
# Every expectation is Rakudo's (2026.09); the file passes on both engines.
# Contract: exit 0 + last line PASS.
my @fail;
sub ck(Bool() $ok, $desc) { @fail.push($desc) unless $ok }
sub is-r($got, $want, $desc) { ck(($got // '(undef)') eq $want, "$desc: got {$got // '(undef)'}, want $want") }

my $out;
# Runs the code; answers what it printed into $out, then the exception's class
# and the first line of its message (or "lived").
sub outcome(Str $code) {
    $out = '';
    my $res = 'lived';
    try {
        EVAL $code;
        CATCH { default { $res = .^name.subst(/'+{X::Comp}'/, '') ~ ': ' ~ .message.lines[0] } }
    }
    "$out|$res"
}
sub nomatch($kw, $name) { "Cannot resolve caller {$kw}({$name}:U); none of these signatures matches:" }

# the koan's program, and last/redo
is-r outcome(q[OUTER: for 1..2 -> $i { for 1..2 -> $j { next OUTER if $j > $i; $out ~= "$i$j " } }; $out ~= 'done']),
     "11 |X::Multi::NoMatch: {nomatch 'next', 'OUTER'}", 'next OUTER under an OUTER label';
is-r outcome(q[OUTER: for 1..2 -> $i { for 1..2 -> $j { last OUTER if $j > $i; $out ~= "$i$j " } }; $out ~= 'done']),
     "11 |X::Multi::NoMatch: {nomatch 'last', 'OUTER'}", 'last OUTER under an OUTER label';
is-r outcome(q[my $n = 0; OUTER: for 1..2 -> $i { $n++; last if $n > 3; redo OUTER if $n == 1; $out ~= "$i " }]),
     "|X::Multi::NoMatch: {nomatch 'redo', 'OUTER'}", 'redo OUTER under an OUTER label';
is-r outcome(q[my $i = 0; OUTER: while $i < 3 { $i++; last OUTER if $i == 2; $out ~= "$i " }]),
     "1 |X::Multi::NoMatch: {nomatch 'last', 'OUTER'}", 'last OUTER under a while labelled OUTER';
is-r outcome(q[for 1..2 -> $i { $out ~= "$i "; next OUTER }]),
     "1 |X::Multi::NoMatch: {nomatch 'next', 'OUTER'}", 'next OUTER with no label at all';

# every pseudo-package that wins over a label of its name
for <MY CORE SETTING UNIT OUTERS CALLER CALLERS DYNAMIC LEXICAL CLIENT> -> $p {
    is-r outcome("$p: for 1..2 -> \$i \{ for 1..3 -> \$j \{ next $p if \$j == 2; \$out ~= \"\$i\$j \" } }"),
         "11 |X::Multi::NoMatch: {nomatch 'next', $p}", "next $p under a $p label";
}
# (OUR is the current package: Rakudo names it GLOBAL here)
ck outcome(q[OUR: for 1..2 -> $i { for 1..3 -> $j { next OUR if $j == 2; $out ~= "$i$j " } }])
       .starts-with('11 |X::Multi::NoMatch: Cannot resolve caller next('), 'next OUR under an OUR label';
# GLOBAL is known while compiling: nothing runs
is-r outcome(q[GLOBAL: for 1..2 -> $i { for 1..3 -> $j { next GLOBAL if $j == 2; $out ~= "$i$j " } }]),
     '|X::TypeCheck::Argument: Calling next(GLOBAL) will never work with any of these multi signatures:',
     'next GLOBAL under a GLOBAL label';

# …and the names a label of that name still takes
for <COMPILING PROCESS PARENT Int Label FOO> -> $p {
    is-r outcome("$p: for 1..2 -> \$i \{ for 1..3 -> \$j \{ next $p if \$j == 2; \$out ~= \"\$i\$j \" } }"),
         '11 21 |lived', "next $p under a $p label";
}

# the bare term is the package, and OUTER:: lookups are untouched
is-r outcome(q[OUTER: for 1 { $out = OUTER.^name }]), 'OUTER|lived', 'OUTER under an OUTER label is the package';
is-r outcome(q[OUTER: for 1..3 -> $i { OUTER.next if $i == 2; $out ~= "$i " }]),
     "1 |X::Method::NotFound: No such method 'next' for invocant of type 'OUTER'", 'OUTER.next';
is-r outcome(q[my $x = 'outer-x'; { my $x = 'inner-x'; OUTER: for 1 { }; $out = $OUTER::x ~ ' ' ~ OUTER::<$x> }]),
     'outer-x outer-x|lived', 'OUTER:: beside an OUTER label';
is-r outcome(q[my $x = 'outer-x'; { my $x = 'inner-x'; OUTER: for 1 { $out = $OUTER::x } }]),
     'inner-x|lived', 'OUTER:: inside a loop labelled OUTER';

# a Label passed as a value names its loop
is-r outcome(q[FOO: for 1..2 -> $i { for 1..3 -> $j { next(FOO) if $j == 2; $out ~= "$i$j " } }]),
     '11 21 |lived', 'next(FOO)';
is-r outcome(q[FOO: for 1..2 -> $i { for 1..3 -> $j { my $l = FOO; next $l if $j == 2; $out ~= "$i$j " } }]),
     '11 21 |lived', 'next $label';
is-r outcome(q[FOO: for 1..2 -> $i { for 1..3 -> $j { last(FOO) if $j == 2; $out ~= "$i$j " } }]),
     '11 |lived', 'last(FOO)';
is-r outcome(q[my $n = 0; FOO: for 1..2 -> $i { for 1..2 -> $j { $n++; my $l = FOO; redo $l if $n == 2; $out ~= "$i$j " } }; $out ~= $n]),
     '11 11 12 21 22 6|lived', 'redo $label';
is-r outcome(q[FOO: for 1..3 -> $i { FOO.next if $i == 2; $out ~= "$i " }]), '1 3 |lived', 'FOO.next';
is-r outcome(q[FOO: for 1..3 -> $i { FOO.last if $i == 2; $out ~= "$i " }]), '1 |lived', 'FOO.last';
is-r outcome(q[my $n = 0; FOO: for 1..2 -> $i { $n++; FOO.redo if $n == 1; $out ~= "$i " }; $out ~= $n]),
     '1 2 3|lived', 'FOO.redo';
is-r outcome(q[my \c = \(); for 1..3 { last |c if $_ == 2; $out ~= "$_ " }]), '1 |lived', 'last |c over an empty capture';

# anything else matches no candidate before 6.e
is-r outcome(q[my $t = Int; for 1..2 -> $i { $out ~= "$i "; next $t }]),
     "1 |X::Multi::NoMatch: {nomatch 'next', 'Int'}", 'next $type-object';
ck outcome(q[for 1..3 { redo 42 if $_ == 2; $out ~= "$_ " }]).starts-with('|X::TypeCheck::Argument: '),
   'redo 42 is refused while compiling';

# from 6.e, `next`/`last` take the package as the iteration's value. (An EVAL
# cannot raise the language revision, so each runs as a program of its own.)
sub run6e(Str $code) {
    my $p = run $*EXECUTABLE, q[-e],
        q[use v6.e.PREVIEW; ] ~ $code ~ q[; CATCH { default { say .^name, ": ", .message.lines[0] } }], :out, :err;
    my $got = $p.out.slurp(:close).trim;
    $p.err.slurp(:close);
    $got
}
is-r run6e(q[say (for 1..3 -> $j { next OUTER if $j == 2; $j }).raku]), q[(1, OUTER, 3)],
     q[6.e: next OUTER is a value];
is-r run6e(q[say (for 1..3 -> $j { last OUTER if $j == 2; $j }).raku]), q[(1, OUTER)],
     q[6.e: last OUTER is a value];
is-r run6e(q[say (for 1..3 { next GLOBAL if $_ == 2; $_ }).raku]), q[(1, GLOBAL, 3)],
     q[6.e: next GLOBAL is a value];
is-r run6e(q[FOO: for 1..2 -> $i { for 1..3 -> $j { next FOO if $j == 2; print "$i$j " } }]), q[11 21],
     q[6.e: next FOO still names the loop];
is-r run6e(q[my $n = 0; for 1..2 { $n++; redo OUTER if $n == 1 }]),
     "X::Multi::NoMatch: {nomatch q[redo], q[OUTER]}", q[6.e: redo OUTER];

if @fail { note "FAIL: $_" for @fail; exit 1 }
say 'PASS';
