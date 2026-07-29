# Regression: two fixes found chasing S17-channel/stress.t.
#   1. `[!after]` — the `!` negation metaop glued to a WORD infix inside a reduce
#      wouldn't parse (the token-run scan stopped at the Ident). The scan now
#      crosses a word op, but ONLY when `!` is the run's first token: `[-x]` is
#      still an array literal and `[:!a]` still a pair, not `-x`/`:!a` reduces.
#   2. Channel.receive BLOCKS until an item arrives. It used to return Nil the
#      moment the queue was empty ("single-thread model"), so every
#      producer/consumer program silently got Nil instead of the sent value.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1. negated word infixes reduce
my @sorted = <e l p r>;
my @jumbled = <p e r l>;
check(([!after] @sorted).Bool,  'True',  'not-after-sorted');
check(([!after] @jumbled).Bool, 'False', 'not-after-jumbled');
check(([!before] @sorted).Bool, 'False', 'not-before-sorted');
check(([!eq] 1, 2).Bool,        'True',  'not-eq');
check(([!eqv] [1], [2]).Bool,   'True',  'not-eqv');
check(([\!eq] 1, 2, 3).join(','), 'True,True,True', 'triangular-not-eq');
# the un-negated and symbolic forms still work
check(([after] @sorted).Bool,   'False', 'after');
check(([!==] 1, 2, 3).Bool,     'True',  'not-numeq');
check(([max] 3, 9, 2),          '9',     'max');

# …and these are NOT reduces
sub x { 4 }
check(([-x]).raku,  '[-4]',      'minus-x-is-array-literal');
my @adv = [:!a];
check("{@adv.elems} {@adv[0].key} {@adv[0].value}", '1 a False', 'adverb-pair-is-array-literal');
check(([Any]).raku, '[Any]',     'type-name-is-array-literal');

# 2. receive blocks for a producer that hasn't run yet
my $c = Channel.new;
start { sleep 0.2; $c.send('late') }
check($c.receive, 'late', 'blocking-receive');

# ...and it blocks for as long as the producer takes. This used to be a 300 ms
# cap, which made the line above a RACE that the 0.2 s producer lost on a loaded
# machine — it flaked the macOS CI job while everything else stayed green, and
# `.receive` answered Nil silently rather than failing loudly. 0.6 s is twice the
# old cap, so this assertion fails outright if the deadline ever comes back.
my $slow = Channel.new;
start { sleep 0.6; $slow.send('slower-than-the-old-cap') }
check($slow.receive, 'slower-than-the-old-cap', 'blocking-receive-outlasts-300ms');

# with nothing running that could ever send, it must still answer rather than hang
my $none = Channel.new;
check(($none.receive // 'Nil').Str, 'Nil', 'receive-with-no-producer-does-not-hang');

# it still drains what is already queued, in order
my $q = Channel.new;
$q.send($_) for 1, 2, 3;
check((^3).map({ $q.receive }).join(','), '1,2,3', 'queued-receive-order');

# and a closed, drained channel still throws rather than blocking
my $z = Channel.new;
$z.close;
my $threw = False;
try { $z.receive; CATCH { default { $threw = True } } }
@fail.push('closed-receive-did-not-throw') unless $threw;

# a worker consuming while the main thread produces
my $w = Channel.new;
my $sum = start { my $t = 0; $t += $w.receive for ^4; $t }
$w.send($_) for 10, 20, 30, 40;
check(await($sum), '100', 'worker-consumer');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
