# Regression: in `%h{EXPR} = RHS` and `@a[EXPR] = RHS`, when EXPR was a
# method call and RHS called back into the same routine, the inner call's
# assignment used the OUTER call's key. An element assignment parks the
# value of a subscript that calls something, so the slot lookup does not
# run it a second time; the right-hand side runs between the park and the
# take, and the inner call evaluated the same node on the same thread. A
# parked value now belongs to the frame that parked it. YAML::Native
# (construct/build-map) worked around it with a temporary variable.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.08 on 2026-10-19.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub f($d) { my %h; %h{$d.Str} = $d ?? f($d - 1) !! 'x'; %h }
ck(f(1).raku, '{"1" => ${"0" => "x"}}', 'a hash key that is a method call');
ck(f(3).raku, '{"3" => ${"2" => ${"1" => ${"0" => "x"}}}}', 'three levels deep');
sub g($d) { my @a; @a[$d.Int] = $d ?? g($d - 1) !! 'x'; @a }
ck(g(1).raku, '[Any, ["x"]]', 'an array index that is a method call');
sub h($d) { my %h; %h{$d.Str} //= $d ?? h($d - 1) !! 'x'; %h }
ck(h(2).raku, '{"2" => ${"1" => ${"0" => "x"}}}', 'and under //=');
sub k($d) { my %h; %h{key($d)} = $d ?? k($d - 1) !! 'x'; %h }
sub key($d) { "k$d" }
ck(k(1).raku, '{:k1(${:k0("x")})}', 'a key that is a sub call');

# the parked key still saves the second run of a subscript's side effects
my $n = 0; sub next-slot { $n++ }
my @s; @s[next-slot()] = "v" for ^3;
ck(@s.raku, '["v", "v", "v"]', 'a subscript call runs once per assignment');
ck($n, 3, 'three runs for three assignments');
my $m = 0; sub mk { "k" ~ $m++ }
my %t; %t{mk()} = 1 for ^2;
ck(%t.keys.sort.List, ('k0', 'k1'), 'and for a hash key');
# an exception between park and take leaves nothing behind for the next one
my %u;
for ^2 -> $i { try { %u{$i.Str} = $i == 0 ?? die("no") !! "y" } }
ck(%u.raku, '{"1" => "y"}', 'a failed assignment does not leak its key into the next');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
