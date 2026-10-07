# A word list whose first word starts with `=`: the lexer fuses `<=` (and
# `<==`) into one infix token, and in term position — after `=`, or spaced
# after a listop like `say` — that token is the list's opening angle plus
# the start of its first word. `say <= a>` died "Missing required term after
# infix". Rakudo reads a spaced `<=` after a listop as a word list always
# (`f <= 3` is an unterminated one there). Found by the sigil-free R&D.
#
# Runs under both engines: Rakudo passes every check natively.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub words(*@w) { @w.join('|') }

check (words <= a>), '=|a', 'after a listop';
check (words <== a>), '==|a', '`<==`';
check <=a b>, ("=a", "b"), 'glued to the first word';
my @x = <=x>; check @x, ["=x"], 'after `=`';
check (1, <= a>), (1, ("=", "a")), 'after a comma';
# …and every infix spelling still is one
my $two = 2;
check 1 <= $two, True, '`<=` infix';
check 3 ≤ 2, False, '`≤` infix';
check (1..3).map(* <= 2).List, (True, True, False), 'in a WhateverCode';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
