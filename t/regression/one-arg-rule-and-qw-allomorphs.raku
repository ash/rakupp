# Regression: three list/literal defects, all found by Crane (issue #69) and all
# of them silent — each produced a plausible value of the wrong SHAPE.
#
# 1. `qw<8 9>` made IntStr allomorphs. Only the ANGLE forms val() their words:
#    `<8 9>`, `«8 9»` and `<<8 9>>` give IntStr, while every q-family spelling —
#    qw, qww, qqw, qqww and any `q:w`/`Q:w`/`:words` adverb — gives plain Str.
#    Since `IntStr ~~ Int` is True this changed DISPATCH: Crane reads a step as a
#    positional index when it is an Int, so `qw<8 9 10>` in a path built a
#    ten-element ARRAY where Rakudo nests three hash keys.
#
# 2. `List.new(...)` flattened its arguments. It takes them AS ELEMENTS:
#    `List.new([1,2])` is a one-element list holding the Array. (`Array.new`
#    DOES flatten — Rakudo agrees there.) Crane's list leaf is
#    `List.new({:path(…), :value(…)})`, which came back as two loose Pairs, and
#    the caller then sorted a flat pair soup with every path away from its value.
#
# 3. A single Hash in an array literal did not spread. `[%h]` is its PAIRS and
#    `[{}]` is the EMPTY array, under the same one-arg rule that already spread
#    `[@a]` and `[1..3]`. Crane's TOML shape `:hello([{}])` is an empty array, so
#    its walk stops at "hello"; with one element it descended a level that does
#    not exist.
#
# Runs under both engines: Rakudo passes every check natively.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- 1. only the ANGLE forms make allomorphs --------------------------------
check qw<8 9>.map({ .^name }).join(','),   'Str,Str',       'qw does not allomorph';
check qww<8 9>.map({ .^name }).join(','),  'Str,Str',       'qww does not allomorph';
check qqw<8 9>.map({ .^name }).join(','),  'Str,Str',       'qqw does not allomorph';
check qqww<8 9>.map({ .^name }).join(','), 'Str,Str',       'qqww does not allomorph';
check q:w/8 9/.map({ .^name }).join(','),  'Str,Str',       'the :w adverb does not allomorph';
check qw<1.5>.map({ .^name }).join(','),   'Str',           'nor for a Rat-looking word';
check <8 9>.map({ .^name }).join(','),     'IntStr,IntStr', 'the bare angle list DOES';
check «8 9».map({ .^name }).join(','),     'IntStr,IntStr', 'and so do guillemets';
check <1.5>.map({ .^name }).join(','),     'RatStr',        'and a Rat-looking word there';
# the consequence that bit: a qw word must not answer an Int dispatch
multi sub step(Int:D $ --> Str:D) { 'int' }
multi sub step($       --> Str:D) { 'key' }
check step(qw<8>[0]), 'key', 'a qw word dispatches as a Str, not an Int';
check step(<8>[0]),   'int', 'an angle word is an allomorph and dispatches as Int';

# --- 2. List.new takes its arguments as ELEMENTS ----------------------------
check List.new([1, 2]).elems,      1, 'List.new([1,2]) is one element';
check List.new({ :a(1) }).elems,   1, 'List.new(hash) is one element';
check List.new(1, 2).elems,        2, 'List.new(1,2) is two';
check List.new().elems,            0, 'List.new() is empty';
my @a = 1, 2;
check List.new(@a).elems,          1, 'List.new(@a) holds the Array whole';
check Array.new([1, 2]).elems,     2, 'Array.new DOES flatten — unchanged';
check Array.new(@a).elems,         2, '…for an @-variable too';

# --- 3. a single Hash in an array literal spreads ---------------------------
check [{}].elems,                  0, '[{}] is the empty array';
check [{ :a(1) }].elems,           1, '[{:a(1)}] is one PAIR';
check [{ :a(1) }][0].^name,   'Pair', '…and it really is a Pair, not a Hash';
my %h = :a(1), :b(2);
check [%h].elems,                  2, '[%h] is its pairs';
check [%h,].elems,                 1, 'a trailing comma takes it out of the one-arg rule';
check [{}, {}].elems,              2, 'two hashes are two elements';
check [{}, {}][0].^name,      'Hash', '…and stay Hashes';
# an ITEMIZED hash is one element: the one-arg rule does not reach through a
# Scalar container
my $x = { :a(1), :b(2) };
check [$x].elems,                  1, '[$x] keeps the Hash whole';
my @r = ({ :a(1), :b(2) },);
check [@r[0]].elems,               1, '[@r[0]] keeps it whole too';

if @fail {
    note $_ for @fail;
    die "{+@fail} check(s) failed";
}
say 'PASS';
