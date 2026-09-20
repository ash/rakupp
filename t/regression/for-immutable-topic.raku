# Regression: `for 1..3 { $_ = 9 }` ran to completion instead of dying.
#
# Rakudo has TWO refusals here and words them differently, because they say
# different things. A readonly CONTAINER — a plain `-> $i` — is "Cannot assign
# to a readonly variable or a value": there is a container and it is closed.
# A raw binding to something with no container at all — the `$_` of `for 1..3`,
# or a parameter declared `is raw` — is "Cannot assign to an immutable value":
# there is nowhere to write. We had only the first state, so every source that
# yields bare values took the write and dropped it on the floor.
#
# `Value::immutableBind` is the second state. It is set WITH `readonly`, so
# every existing check still fires and it only picks the message, and it costs
# nothing: it lands in padding the flag block already had.
#
# Which sources yield bare values is a WHITELIST (immutableLoopSource), not a
# guess. Marking a source immutable because we merely failed to find its alias
# would turn working programs into crashes — `for @a.grep(…)` and `for f()`
# where `f` returns `@g` both alias under Rakudo and neither aliases here yet.
# The last section pins those down: they must stay quiet, not start dying.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged, so it doubles
# as the oracle that says these are Rakudo's answers and not ours — every
# expectation below was checked against Rakudo 2026.08, 2026-09-20.
use MONKEY-SEE-NO-EVAL;
my @fail;

sub msg-of($code) {
    my $m = '';
    { EVAL $code; CATCH { default { $m = .Str } } }
    $m
}
sub immutable($desc, $code) {
    my $m = msg-of($code);
    @fail.push("$desc: no die") unless $m;
    @fail.push("$desc: wrong message ($m)")
        if $m && !$m.contains('Cannot assign to an immutable value');
}
sub readonly($desc, $code) {
    my $m = msg-of($code);
    @fail.push("$desc: no die") unless $m;
    @fail.push("$desc: wrong message ($m)")
        if $m && !$m.contains('Cannot assign to a readonly variable or a value');
}
sub lives($desc, $code) {
    my $m = msg-of($code);
    @fail.push("$desc: died ($m)") if $m;
}

# a source that yields BARE VALUES: the write has nowhere to land
immutable('Range',            'for 1..3 { $_ = 9 }');
immutable('list literal',     'for (1,2,3) { $_ = 9 }');
immutable('comma list',       'for 1,2,3 { $_ = 9 }');
immutable('word list',        'for <a b c> { $_ = 9 }');
immutable('lone Int',         'for 42 { $_ = 9 }');
immutable('lone Str',         'for "abc" { $_ = 9 }');
immutable('.values on a literal', 'for (1,2,3).values { $_ = 9 }');
immutable('.reverse on a literal', 'for (1,2,3).reverse { $_ = 9 }');
immutable('is raw over a Range',  'for 1..3 -> $i is raw { $i = 9 }');
immutable('is raw over a list',   'for (1,2,3) -> $i is raw { $i = 9 }');
# .kv and .pairs build fresh keys and Pairs rather than passing elements along,
# and .List decontainerises — for an Array and a Hash alike
immutable('@a.kv',    'my @a = 1,2,3; for @a.kv { $_ = 9 }');
immutable('@a.pairs', 'my @a = 1,2,3; for @a.pairs { $_ = 9 }');
immutable('@a.List',  'my @a = 1,2,3; for @a.List { $_ = 9 }');
immutable('%h.kv',    'my %h = a => 1; for %h.kv { $_ = 9 }');
immutable('%h.pairs', 'my %h = a => 1; for %h.pairs { $_ = 9 }');
# `list(@a, @b)` flattens into a NEW List; `list(@a)` alone is the array's own
# view and aliases. The topic holds an Array here, so this also pins that the
# BINDING is reported rather than the value in it.
immutable('list of two arrays', 'my @a=1,2; my @b=3,4; for list(@a,@b) { $_ = 9 }');

# a plain `-> $i` is a readonly CONTAINER and keeps that wording, even over a
# source that has no containers at all
readonly('-> $i over a Range', 'for 1..3 -> $i { $i = 9 }');
readonly('-> $i over an array', 'my @a = 1,2,3; for @a -> $i { $i = 9 }');
# …and the traits are PER-PARAMETER: `$a` aliases, `$b` refuses
readonly('the untraited one of two',
         'my @a = 1,2,3; for @a -> $a is rw, $b { $a = 8; $b = 9 }');

# everything that may write still writes
sub check($desc, $got, $want) {
    @fail.push("$desc: got {$got.gist}, want {$want.gist}") unless $got eqv $want;
}
my @t = 1, 2, 3; for @t { $_ = 9 };            check('$_ over an array', @t, [9,9,9]);
my @v = 1, 2, 3; for @v.values { $_ = 9 };     check('$_ over .values', @v, [9,9,9]);
my @d = 1, 2, 3; for @d <-> $x { $x = 9 };     check('<->', @d, [9,9,9]);
my @w = 1, 2, 3; for @w -> $x is rw { $x = 9 };check('is rw', @w, [9,9,9]);
my %h = a => 1, b => 2; for %h.values { $_ = 9 };
check('%h.values', %h.sort.list, (a => 9, b => 9));
my @c; for 1..3 -> $i is copy { $i = 9; @c.push($i) };
check('is copy', @c, [9,9,9]);

# THE SOUNDNESS GUARD. Rakudo aliases through all of these, so none of them may
# be marked immutable — a whitelist that over-fires turns working code into a
# crash, which is worse than the silent write it replaces. `[1,2,3]` is an
# Array literal and its elements ARE containers, unlike `<a b>`.
lives('a bracket Array literal', 'for [1,2,3] { $_ = 9 }');
lives('.grep of an array',       'my @a = 1,2,3; for @a.grep(* > 1) { $_ = 9 }');
lives('.reverse of an array',    'my @a = 1,2,3; for @a.reverse { $_ = 9 }');
lives('.sort of an array',       'my @a = 1,2,3; for @a.sort { $_ = 9 }');
lives('a sub returning an array', 'my @g = 1,2,3; sub f() { @g }; for f() { $_ = 9 }');
lives('flat of two arrays',      'my @a=1,2; my @b=3,4; for flat(@a,@b) { $_ = 9 }');
lives('list of ONE array',       'my @a = 1,2,3; for list(@a) { $_ = 9 }');
lives('an empty list',           'for () { $_ = 9 }');

# reading is untouched
my @seen; for 1..3 { @seen.push($_) };  check('reading the topic', @seen, [1,2,3]);
my @r;    for <a b> -> $x { @r.push($x) }; check('reading a parameter', @r, ['a','b']);

die @fail.join('; ') if @fail;
say "PASS";
