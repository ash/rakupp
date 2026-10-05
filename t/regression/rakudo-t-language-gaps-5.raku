# Regression: language behaviour from the fifth batch of Rakudo t/ gap files
# (2026-10-05).
#
# - An explicit proto is where its group was declared (`&f.line`).
# - `Int() @c` dispatches as a Positional[Int] (an Array[Int], not any Array).
# - `.fail` after `.close` leaves a Channel's `.closed` Kept.
# - A role group's variants are told apart by a required named parameter.
# - Two terms in a row after a multi-line quote names where it opened.
# - 6.e `%f` renders a Num as C does (ties to even, exact expansion) and a Rat
#   exactly, ties to even; `%F` uppercases Inf.
# - `cmp-ok` finds an operator the caller declared, and an unknown one is a
#   failed test; `skip 2, 'reason'` (backwards) dies.
#
# Every expectation below was checked against Rakudo.

use Test;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "# ok - $desc" }
    else { $fails++; say "# FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

proto sub zp(|) {*}
multi sub zp() { 1 }
ck(&zp.line, 24, 'an explicit proto reports its own line');

multi ga(Str $n, Int() @c) { 'coerced' }
multi ga(Str $n,        @c) { 'plain'   }
ck((ga('x', [1, 2]), ga('x', Array[Int].new(1, 2))), ('plain', 'coerced'), 'a coercive @ parameter dispatches by element type');

my $c = Channel.new;
$c.send(1); $c.close; $c.fail('late'); $c.poll;
ck($c.closed.status, Kept, 'fail after close keeps the closed promise');

role V[::T]           { method which { 'a' } }
role V[::T, :$extra!] { method which { 'b' } }
ck((V[Int].new.which, V[Int, :extra].new.which), ('a', 'b'), 'a role variant chosen by its named parameter');

ck((try { EVAL qq{say "a\nb" 1;}; 'lived' }) // $!.message,
   'Two terms in a row (runaway multi-line "" quote starting at line 1 maybe?)', 'the runaway-quote hint');

my $six-e = run($*EXECUTABLE, '-e',
    q[use v6.e.PREVIEW; print join '|', sprintf('%.0f', 2.5e0), sprintf('%.20f', 0.1e0), sprintf('%.0f', 2.5), sprintf('%.2f', 0.125), sprintf('%F', -Inf)],
    :out).out.slurp(:close);
ck($six-e, '2|0.10000000000000000555|2|0.12|-INF', '6.e %f and %F');

sub infix:<◀> { $^a < $^b }
cmp-ok 1, '◀', 2, 'cmp-ok finds a declared operator by name';
todo 'an unknown operator fails';
cmp-ok 2, 'no-such-op', 2;
ck((try { skip 2, 'reason'; 'lived' }) // 'died', 'died', 'skip with its arguments backwards dies');

done-testing;
say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
