# Regression: `for 1..3 -> $i { $i = 9 }` ran to completion instead of dying.
#
# A plain `-> $i` is a readonly parameter — the same rule a sub's or a block's
# `$x` follows, and rakupp already enforced it for both. The `for` statement
# binds its loop variable on its own paths (an integer-Range fast path that
# mints `Value::integer(k)` directly, and asTopic for everything else) and
# neither marked the binding, so the assignment was accepted and then dropped:
# nothing aliases the source, so the 9 went nowhere and the loop kept going.
#
# Silence is the wrong failure here. The write LOOKS like it edits the list,
# and the program that means it has to be told to say `<->`; accepting it
# instead lets code be written against rakupp that Rakudo refuses.
# Roast S04-statements/for.t test 23 ("-> $var is ro by default") covers it.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged, so it
# doubles as the oracle that says these are Rakudo's answers and not ours —
# every expectation below was checked against Rakudo 2026.08, 2026-09-20.
use MONKEY-SEE-NO-EVAL;
my @fail;

sub dies-ro($desc, $code) {
    my $msg = '';
    { EVAL $code; CATCH { default { $msg = .Str } } }
    @fail.push("$desc: no die") unless $msg;
    @fail.push("$desc: wrong message ($msg)")
        if $msg && !$msg.contains('Cannot assign to a readonly variable or a value');
}

# every source shape, and the whole signature — not just the first variable
dies-ro('Range',          'for 1..3 -> $i { $i = 9 }');
dies-ro('List',           'for (1,2,3) -> $i { $i = 9 }');
dies-ro('quote-words',    'for <a b c> -> $i { $i = 9 }');
dies-ro('array',          'my @a = 1,2,3; for @a -> $i { $i = 9 }');
dies-ro('array slice',    'my @a = 1,2,3; for @a[0,1] -> $i { $i = 9 }');
dies-ro('hash',           'my %h = a => 1; for %h -> $p { $p = 9 }');
dies-ro('first of two',   'for 1..4 -> $a, $b { $a = 9 }');
dies-ro('second of two',  'for 1..4 -> $a, $b { $b = 9 }');
dies-ro('destructured',   'my @p = (1,2),(3,4); for @p -> ($a, $b) { $a = 9 }');
# a mutating match is an assignment through the same container
dies-ro('s/// on loop var', 'my @a = <aa bb>; for @a -> $i { $i ~~ s/a/x/ }');

# `$^a` makes the body an arity-1 block exactly as `-> $a` does, so it is
# readonly for the same reason. Written out rather than EVAL'd: a placeholder
# inside an EVAL string is refused here for an unrelated reason.
my $ph = '';
{ for 1..3 { $^a = 9 }; CATCH { default { $ph = .Str } } }
@fail.push('placeholder: no die') unless $ph;
@fail.push("placeholder: wrong message ($ph)")
    if $ph && !$ph.contains('Cannot assign to a readonly variable or a value');

# …and the spellings that ARE writable stay writable
my @rw = 1, 2, 3;
for @rw <-> $i { $i = 9 }
@fail.push('<-> stopped aliasing') unless @rw eqv [9, 9, 9];

my @rwt = 1, 2, 3;
for @rwt -> $i is rw { $i = 8 }
@fail.push('is rw stopped aliasing') unless @rwt eqv [8, 8, 8];

my @topic = 1, 2, 3;
for @topic { $_ = 7 }
@fail.push('$_ stopped aliasing') unless @topic eqv [7, 7, 7];

my @copies;
for 1..3 -> $i is copy { $i = 9; @copies.push($i) }
@fail.push('is copy stopped being writable') unless @copies eqv [9, 9, 9];

# `-> @inner` binds the array itself: `.push` mutates the object, it does not
# assign to the container, so only `$` may be marked (as bindParams does)
my @aoa = [1, 2], [3, 4];
for @aoa -> @row { @row.push(9) }
@fail.push('-> @row lost its push') unless @aoa eqv [[1,2,9], [3,4,9]];

# reading, copying out and closing over the loop variable are all untouched —
# and the flag must not travel with the value into whatever it is copied into
my @seen;
for 1..3 -> $i { @seen.push($i) }
@seen[0] = 5;
@fail.push('the flag followed the value out of the loop') unless @seen eqv [5, 2, 3];

my @closures;
for 1..3 -> $i { @closures.push(-> { $i * 2 }) }
@fail.push('closures over the loop var broke') unless @closures.map({ .() }).list eqv (2, 4, 6);

my $sum = 0;
for 1..3 -> $i { my $c = $i; $c = $c * 2; $sum += $c }
@fail.push('a copy of the loop var was not writable') unless $sum == 12;

# the loop still runs: `last`, `next` and the value form are all unaffected
my @ran;
for 1..5 -> $i { next if $i == 2; last if $i == 4; @ran.push($i) }
@fail.push('last/next broke') unless @ran eqv [1, 3];
@fail.push('the value form broke') unless (for 1..3 -> $i { $i * 2 }) eqv (2, 4, 6);

die @fail.join('; ') if @fail;
say "PASS";
