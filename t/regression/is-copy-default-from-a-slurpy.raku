# Regression: an `is copy` parameter is a fresh, writable container, whatever
# was bound to it.
#
# A slurpy's elements are read-only values (Rakudo's are too), and the general
# binder — the one an `is copy` parameter WITH A DEFAULT goes through — never
# set an `is copy` parameter read-only, but never cleared what came in either.
# So `$offset--` died "require mutable arguments" when the argument arrived
# through `*@args` and `|@args`. Text::CSV's getline_all takes
# `Int $offset is copy = 0`, and its t/77_getall.t calls it exactly that way;
# `rakupp install Text::CSV` refused the dist.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

sub plain(Int $o is copy = 0) { $o--; $o }
sub untyped($o is copy = 0) { $o++; $o }
sub two(Int $o is copy = 0, Int $l is copy = -1) { $o--; $l--; ($o, $l) }
sub named(:$n is copy = 1) { $n++; $n }
sub via(*@args)   { plain(|@args) }
sub viau(*@args)  { untyped(|@args) }
sub via2(*@args)  { two(|@args) }
sub vian(*%args)  { named(|%args) }
sub viac(|c)      { plain(|c) }
class K { method m($io, Int $offset is copy = 0) { $offset-- > 0 ?? 'skip' !! 'take' } }
sub viam(*@args)  { K.m(1, |@args) }

check 'typed, through a slurpy',     via(3),      2;
check 'untyped, through a slurpy',   viau(3),     4;
check 'two, through a slurpy',       via2(3, 5),  (2, 4);
check 'one of two given',            via2(3),     (2, -2);
check 'none given',                  via(),       -1;
check 'named, through a slurpy hash', vian(n => 5), 6;
check 'through a capture',           viac(3),     2;
check 'a method, as Text::CSV has it', viam(1),   'skip';
check '…and once it has counted down', viam(0),   'take';
check 'from a for topic', do { my @r; for 1..2 -> $i { @r.push: plain($i) }; @r }, [0, 1];

# the argument itself is left alone, and a plain parameter is still read-only
{
    my $x = 5;
    plain($x);
    check 'the caller keeps its value', $x, 5;
    sub ro($y = 0) { $y++ }
    check 'a plain parameter stays read-only', (try { ro(1); 'lived' }) // 'died', 'died';
}

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
