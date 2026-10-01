# Regression: Perl 5's `"@{…}"` and `"${…}"` compiled inside a string — `"x
# @{at}"` called `at` and printed its value — where Rakudo refuses them at
# compile time with X::Obsolete. `@{name}` outside a string called a routine.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;

for Q{sub at { 1 }; "x @{at}"} => ('@{at}', '{@at}'),
    Q{"x ${x}"} => ('${x}', '{$x}'),
    Q{my @a; "x @{@a}"} => ('@{@a}', '@(@a) for hard ref or @::(@a) for symbolic ref'),
    Q{"a@{1+1}b"} => ('@{1+1}', '@(1+1) for hard ref or @::(1+1) for symbolic ref'),
    Q{@{a}} => ('@{a}', '@a'),
    Q{${a}} => ('${a}', '$a') -> (:key($src), :value(($old, $repl))) {
    my $got = 'compiled';
    EVAL $src;
    CATCH { default { $got = .^name eq 'X::Obsolete' ?? "{.old}|{.replacement}" !! .^name } }
    @fail.push("$src gave $got") unless $got eq "$old|$repl";
}
# not Perl 5: these still work
@fail.push("single quotes") unless EVAL(Q{'x @{at}'}) eq 'x @{at}';
@fail.push("escaped") unless EVAL(Q{"x @\{y\}"}) eq 'x @{y}';

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
