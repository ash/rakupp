# Issue #193: `iz4 gate --json` and `iz4 status` under Raku++ died about one
# run in a hundred with "Undefined routine 'v'" or "Variable '$v' is not
# declared", naming a sub's own parameter, in the runs where the tool's tests
# ran on several `start` threads. The report suspected the module cache, which
# was off; ThreadSanitizer found two races in what a thread does when it only
# READS:
#
# 1. A plain Hash's Pairs hold its own containers (ValueHash::aliasOf), and
#    the FIRST one handed out moved the hash's entries into a generation the
#    Pairs co-own. Handing out a Pair was a write: two threads taking `.pairs`
#    of one shared hash both made the move, the second moving an emptied list,
#    so the hash read as `{}` there, or its Pairs pointed into a list already
#    freed (wrong values, SIGSEGV). `run` reading %*ENV on one thread while
#    another did `%*ENV.grep(…)` read the list mid-move, and that is iz4
#    status's shape: its driver thread starts a process while the main thread
#    builds the tests' environment. The entries now stay where they are until
#    the hash lets them go, and the generation is made once, under a lock.
# 2. A sub's first call decides whether later calls take the lean lane
#    (callPlainSub), and the decision was published with a relaxed flag, so a
#    thread could take the lane with the slot layout another thread was still
#    building.
#
# Eight `start` blocks encode one shared structure with the issue's
# json-encode, a fresh encoder and a fresh structure each of twenty rounds:
# before, most runs of this program printed a wrong encoding or died. What a
# hash's Pairs do, which the first fix moved around, is checked after.
#
# Every answer below is Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub check($what, $got, $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my constant ENCODER = Q:to/END/;
    sub json-encode($v --> Str) {
        return 'null' without $v;
        given $v {
            when Bool        { $v ?? 'true' !! 'false' }
            when Str         { json-str($v) }
            when Numeric     { $v.Str }
            when Associative { '{' ~ $v.pairs.sort(*.key).map({ json-str(.key.Str) ~ ':' ~ json-encode(.value) }).join(',') ~ '}' }
            when Positional  { '[' ~ $v.map({ json-encode($_) }).join(',') ~ ']' }
            default          { json-str($v.Str) }
        }
    }
    sub json-str(Str $s --> Str) { '"' ~ $s.subst(/ <[\\"]> /, { '\\' ~ $_ }, :g) ~ '"' }
    &json-encode
    END

sub doc($r) {
    %( a => [1, 2, 'x', %( q => True )], b => True, c => %( d => Nil, e => "z\"$r" ),
       f => [[$r, [2, %( g => 3 )]], 4], :h(%( (^6).map({ "k$_" => $_ * $r }) )) )
}

# the issue's encoder, its first calls on eight threads over one shared hash
my $bad = 0;
for ^20 -> $r {
    my &encode = EVAL ENCODER;
    my %d = doc($r);
    my $go = Promise.new;
    my @p = (^8).map: { start { await $go; encode(%d) } };
    $go.keep;
    my @got = @p.map({ (try .result) // 'died' });
    my $want = encode(doc($r));   # a twin of %d, after the threads
    $bad++ if @got.grep(* ne $want);
}
check 'eight threads encoding one shared hash, 20 rounds: rounds with a wrong answer', $bad, 0;

# one thread iterating a hash while others take its first Pairs
$bad = 0;
for ^20 -> $r {
    my %h = (^40).map({ "key$_" => $_ + $r });
    my $go = Promise.new;
    my @p = (^8).map: -> $w {
        start {
            await $go;
            $w %% 2 ?? %h.keys.sort.join(',') !! %h.pairs.sort(*.key).map(*.value).sum
        }
    };
    $go.keep;
    my @got = @p.map({ (try .result) // 'died' });
    my $keys = (^40).map({ "key$_" }).sort.join(',');
    my $sum = (^40).map(* + $r).sum;
    $bad++ if @got.kv.grep(-> $w, $g { $g ne ($w %% 2 ?? $keys !! $sum) });
}
check 'keys on four threads, first Pairs on four, 20 rounds: rounds with a wrong answer', $bad, 0;

# a Pair holds the hash's own container
{
    my %h = a => 1, b => 2;
    my $p = %h.pairs.first(*.key eq 'a');
    $p.value = 5;
    check 'a write through a Pair reaches the hash', %h<a>, 5;
    %h<a> = 7;
    check 'a write to the hash shows through its Pair', $p.value, 7;
    %h<b>:delete;
    check 'a deleted key leaves the other Pair', $p.value, 7;
    %h = ();
    check '…which keeps its container after the hash is emptied', $p.value, 7;
    $p.value = 9;
    check '…and a write to it no longer reaches the hash', %h.elems, 0;
    %h<a> = 1;
    check '…nor does the refilled hash reach it', $p.value, 9;
}
{
    my $p;
    { my %h = x => 3; $p = %h.pairs[0] }
    check 'a Pair outlives its hash', $p.value, 3;
    my @kept;
    my %h;
    for ^500 { %h = n => $_; @kept.push(%h.pairs[0]) if $_ %% 100 }
    check 'Pairs of a hash refilled in a loop keep their values', @kept.map(*.value).List, (0, 100, 200, 300, 400);
    my %a = q => 1;
    my $qa = %a.pairs[0];
    my %b = r => 2;
    my $rb = %b.pairs[0];
    %a = %b;
    check 'a hash assigned over keeps its old Pair', $qa.value, 1;
    check '…and the assigned one keeps its own', ($rb.value, %a<r>), (2, 2);
}

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
