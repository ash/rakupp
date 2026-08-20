# Regression: `:=` binds at LIST precedence, whatever the target's sigil is.
# `my $b := 1, 2` binds the List (1, 2) — where `my $b = 1, 2` is item
# assignment and warns about the 2 — and it is looser than the zip/cross
# infixes, so `my $s := 2..* Z* 2..*` binds the whole zip. rakupp bound at
# item-assignment precedence, so the scalar took only the first element and
# the rest of the expression was left in sink context.
#
# The zip itself is the second half: two ENDLESS sides (an infinite Range or an
# infinite lazy list) have no finite answer to compute up front, and Rakudo's
# is lazy — `2..* Z* 2..*` is the perfect squares. rakupp flattened both sides
# and produced nonsense. Found in a Weekly Challenge solution that generates
# perfect squares exactly that way.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- binding takes the comma list ---
my $b := 1, 2;
check $b.raku, '(1, 2)', 'a scalar binding takes the whole list';
my $one := 5;
check $one, 5, '…and a single value still binds';
my @r := 1, 2;
check @r.elems, 2, 'an array binding is unchanged';
my $z := 1, 2 and my $ran = True;
check ($ran // False), True, '…and `and` is still looser than the binding';
check $z.raku, '(1, 2)', '…which did not swallow the binding';
# assignment is NOT list-precedence for a scalar target
my $item = (1, 2);
check $item.raku, '$(1, 2)', 'item assignment is untouched';
# in an argument list, the binding takes the comma too
sub f($x, $y?) { "{$x.raku}/{$y.raku}" }
my $a;
check f($a := 1, 2), '$(1, 2)/Any', 'a binding inside an argument list takes the list';

# --- a zip of two endless sides is lazy ---
my $sq := 2..* Z* 2..*;
check $sq.head(4).join(' '), '4 9 16 25', 'the perfect squares, lazily';
# a Seq is consumed once, so the next check binds its own
my $sq2 := 2..* Z* 2..*;
check $sq2.first(* >= 30), 36, '…and .first walks as far as it needs';
my $sum := 1..* Z+ 1..*;
check $sum.head(3).join(' '), '2 4 6', 'an endless zip with another operator';
my $tup := 2..* Z, 2..*;
check $tup.head(2).map(*.join('')).join(' '), '22 33', '…and Z, makes tuples';
# a finite side still stops the zip where it ends
my @two = 1, 2;
check (@two Z+ 1..*).join(' '), '2 4', 'a finite side bounds an endless one';
check (1..3 Z* 1..3).join(' '), '1 4 9', 'and two finite sides are unchanged';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
