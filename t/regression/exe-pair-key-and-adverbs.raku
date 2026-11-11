# Regression: Pair keys and subscript adverbs in a program compiled with --exe.
#   * `$pair<a>:exists` was False: the native adverb helper looked in a Hash
#     only, where the interpreter's lookup knows a Pair's one key;
#   * an adverb on a slice — `%h<a b>:exists`, `%h<>:k`, `@a[0, 1]:v` — went
#     to the same helper, which takes one key, and answered nothing; it now
#     bundles with the interpreter;
#   * `.dynamic` asks the VARIABLE (`my @c is dynamic; @c.dynamic` is True),
#     which a native local cannot answer: it bundles too;
#   * a Whatever held in a variable lost its type as a Pair's key: `my $w = *;
#     ($w => 1).raku` was `"*" => 1`, where the interpreter keeps `* => 1`.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my $pair = a => 5;
ck $pair<a>:exists, True, 'a Pair has its key';
ck $pair<b>:exists, False, '…and no other';
my %h = a => 1, b => 2;
ck (%h<a zz>:exists), (True, False), ':exists on a slice';
ck (%h<>:k).sort.List, <a b>, ':k on a zen slice';
my @a = 10, 20, 30;
ck (@a[0, 2]:v), (10, 30), ':v on a positional slice';
my @c is dynamic;
ck @c.dynamic, True, '.dynamic asks the variable';
my $w = *;
ck ($w => 1).raku, '* => 1', 'a Whatever key';
ck ($w => 1).key.^name, 'Whatever', '…keeps its type';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
