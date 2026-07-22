# Regression: corpus round-2 fixes, batch 2 (2026-07-22).
# 1. bare `$` is an anonymous STATE variable — one persistent slot per textual
#    occurrence (`say ++$` numbers lines). First version stole `$<name>`
#    captures (suite caught it: grammar examples died) — the glued-< form is
#    exempt. It must also still warn in sink context (Roast misc.t:38).
# 2. substitution replacements decode qq escapes: s/x/\n/ inserts a newline,
#    not the letter n (both the subst and match-adverb interpolators).

my $ok = True;

my @n;
for 1..3 { @n.push(+(++$)) }
unless @n eqv [1, 2, 3] {
    say "FAIL: ++\$ did not persist: {@n.raku}";
    $ok = False;
}
my @two;
for 1..2 { @two.push(~(++$ ~ '.' ~ ++$)) }
unless @two eqv ['1.1', '2.2'] {
    say "FAIL: two anon \$s not independent: {@two.raku}";
    $ok = False;
}

# $<name> capture sugar must be untouched
'ab' ~~ / (a) $<tail>=(b) /;
unless $<tail> eq 'b' && $0 eq 'a' {
    say 'FAIL: $<name> capture broken';
    $ok = False;
}

my $s = "x-y";
$s ~~ s:g/\-/\t/;
unless $s eq "x\ty" {
    say "FAIL: replacement \\t: {$s.raku}";
    $ok = False;
}
my $t = "ab";
$t ~~ s/b$/B\n/;
unless $t eq "aB\n" {
    say "FAIL: replacement \\n: {$t.raku}";
    $ok = False;
}

say 'PASS' if $ok;
