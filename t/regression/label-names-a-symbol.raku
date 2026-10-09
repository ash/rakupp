# Regression: a label is a symbol of its scope (2026-10-09).
#
# A label named EXPORT at the top of a program was accepted. The mainline has
# an EXPORT package of its own, so Rakudo reports X::Redeclaration there; in a
# nested block, in a sub, after `unit module` (whose statements are the
# package's) and in an EVAL there is nothing to redeclare. A label and a class
# of one name in one scope are a redeclaration too, in either order.
#
# Expectations checked against Rakudo 2026.09 (/opt/homebrew/bin/rakudo).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

sub compiles(Str $code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    my $err = $p.err.slurp(:close);
    $p.out.slurp(:close);
    $err.contains('Redeclaration') ?? 'redeclaration' !! $p.exitcode == 0 ?? 'lived' !! "died: $err";
}
check compiles('EXPORT: for ^2 { last EXPORT }'),                 'redeclaration', 'EXPORT at the top';
check compiles('{ EXPORT: for ^2 { last EXPORT } }'),             'lived', '…in a block it is free';
check compiles('sub f { EXPORT: for ^2 { last EXPORT } }; f'),    'lived', '…and in a sub';
check compiles('unit module M; EXPORT: for ^2 { last EXPORT }'),  'lived', '…and after `unit module`';
check compiles('unit class C; EXPORT: for ^2 { last EXPORT }'),   'lived', '…and after `unit class`';
check compiles('OUTER2: for ^2 { last OUTER2 }'),                 'lived', 'another name is free';

sub evals(Str $code) {
    my $r = (try EVAL $code) // $!;
    $r ~~ X::Redeclaration ?? $r.symbol !! $r ~~ Exception ?? $r.^name !! 'lived';
}
check evals('EXPORT: for ^2 { last EXPORT }; 1'),            'lived', 'an EVAL has no EXPORT to redeclare';
check evals('Foo: for ^2 { last Foo }; class Foo {}; 1'),    'Foo',   'a label, then a class of its name';
check evals('class Bar {}; Bar: for ^2 { last Bar }; 1'),    'Bar',   '…and the other way round';
check evals('Baz: for ^2 { last Baz }; { class Baz {} }; 1'), 'lived', '…but not across scopes';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
