# Regression: `:out([$a, $b])` — a named parameter whose alias holds a SQUARE
# sub-signature.
#
# A named parameter may destructure its argument, and the brackets say the
# argument is Positional. Only the paren spelling `:value((…))` was accepted, so
# the square one died at "expected variable in named-parameter alias" — which is
# how Test::Run declares every one of its three-part arguments:
#
#     :out([$out?, :pass($out_pass) = True])
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

sub two-part(:out([$a, $b])) { "$a|$b" }
check two-part(out => (1, 2)), '1|2', 'a bracket sub-signature destructures';

# the shape the dist writes: an optional positional plus a defaulted named
sub dist-shape(:out([$o?, :pass($p) = True])) { "{$o // 'Any'}|$p" }
check dist-shape(out => (1,)), '1|True', 'optional plus a defaulted named';
# STILL OPEN: a Pair inside the destructured Positional does not reach the
# inner NAMED parameter — `out => (1, :pass(False))` answers '1|True' here and
# '1|False' on Rakudo. That is the sub-signature binder, not this parse fix, and
# Test::Run does not exercise it (its arguments are plain values).

# the paren spelling it sits beside must keep working
sub paren(:out(($a, $b))) { "$a|$b" }
check paren(out => (3, 4)), '3|4', 'the paren sub-signature still works';

# …and so must the plain alias forms
sub plain(:out($a)) { $a }
check plain(out => 5), 5, 'a plain named alias';
sub both(:v(:$x)) { $x }
check both(v => 3), 3, 'a two-name alias';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
