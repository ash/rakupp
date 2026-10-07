# What an undefined `$` variable VIVIFIES lives in that Scalar, itemized, as
# an assigned `my $x = [1]` does: `my $x; $x.push(1); $x.raku` is `$[1]` and
# `my $h; $h<k> = 1; $h.raku` is `${:k(1)}`. Elements already did this
# (`%h<a>.push: 1` is `$[1]`); the variable itself came out bare — `[1]`,
# `{:k(1)}`. Found by the sigil-free R&D.
#
# Runs under both engines: Rakudo passes every check natively.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

{ my $x; $x.push(1);    check $x.raku, '$[1]', '.push' }
{ my $x; $x.append(1,2); check $x.raku, '$[1, 2]', '.append' }
{ my $x; $x.unshift(1); check $x.raku, '$[1]', '.unshift' }
{ my $x; $x[0] = 1;     check $x.raku, '$[1]', 'a positional store' }
{ my $h; $h<k> = 1;     check $h.raku, '${:k(1)}', 'an associative store' }
{ my $h; $h<a><b> = 1;  check $h.raku, '${:a(${:b(1)})}', 'nested' }
{ my $x; $x.push(1, 2); my $n = 0; $n++ for $x; check $n, 1, 'iterates as one item' }
{ my $x; $x.push(1, 2); check @$x.raku, '[1, 2]', '…and `@$x` its elements' }
{ my %h; %h<a><b> = 1;  check %h.raku, '{:a(${:b(1)})}', 'a hash element, as before' }

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
