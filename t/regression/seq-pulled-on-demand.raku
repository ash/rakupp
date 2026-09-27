# Regression: Seqs that are produced as they are read (ROAST-TRACKS-PLAN
# track B, phase B3, 2026-09-27), and what a slurpy makes of a Seq.
#
# - `Seq.new($iterator)` pulled its iterator dry when the Seq was MADE; now it
#   pulls as the Seq is read, and a PredictiveIterator's `.count-only` answers
#   `.elems` / `.Numeric` without pulling, and lets `.tail` skip to the end.
# - `.squish(:as, :with)` called every callback up front; now once per element
#   (and once per adjacent pair) as the reader gets there.
# - `+@a` handed one Seq binds a List; a sigilless `+a` is always a List.
# - `Buf.iterator` iterated the Buf as ONE item.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/raku — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my class Counter does Iterator {
    has $.n = 0;
    has @.log;
    method pull-one { @!log.push('pull'); $!n < 3 ?? ++$!n !! IterationEnd }
}
{
    my $it = Counter.new;
    my $s = Seq.new($it);
    ck($it.log.elems, 0, 'Seq.new($iterator) pulls nothing when made');
    ck($s[0], 1, 'the first element');
    ck($it.log.elems, 1, '…pulled once');
    ck($s.List, (1, 2, 3), 'all of it');
}

my class Predictive does PredictiveIterator {
    has $.i = 0;
    has @.log;
    method pull-one { @!log.push('pull'); ++$!i <= 5 ?? $!i !! IterationEnd }
    method skip-one { @!log.push('skip'); ++$!i <= 5 }
    method count-only { @!log.push('count'); 0 max 5 - $!i }
}
{
    my $it = Predictive.new;
    ck(Seq.new($it).elems, 5, '.elems of a Seq over a PredictiveIterator');
    ck($it.log.join(' '), 'count', '…asks .count-only and pulls nothing');
    my $jt = Predictive.new;
    ck(Seq.new($jt).tail, 5, '.tail');
    ck($jt.log.grep('pull').elems, 1, '…pulls only the element it answers with');
}

{
    my @as; my @with;
    my $as   = { @as.push: $_; $_ };
    my $with = { @with.push: "$^a $^b"; $^a + 1 == $^b };
    my $i := (1, 2, 3, 2, 1, 0).squish(:$as, :$with).iterator;
    ck($i.pull-one, 1, 'squish: the first element');
    ck((+@as, +@with), (1, 0), '…one :as call and no :with call so far');
    ck($i.pull-one, 2, 'the second');
    ck((+@as, +@with), (4, 3), '…read only as far as it needed');
    ck((1, 1, 2, 2, 3).squish(:with(&[==])).List, (1, 2, 3), 'squish :with, read whole');
}

{
    sub f(+@a) { @a }
    ck(f((1, 2, 3).grep({ $_ })).WHAT, List, '`+@a` handed one Seq is a List');
    ck(f((1, 2, 3).grep({ $_ })), (1, 2, 3), '…of its values');
    ck(f([1, 2, 3]).WHAT, Array, '…and handed anything else, an Array');
    ck((try f((1, 2).grep({ $_ })).push(3)) // $!.^name, 'X::Immutable', '…which is immutable');
    sub g(+a) { a }
    ck(g([1, 2]).WHAT, List, 'a sigilless `+a` is a List');
    ck(g(1, 2), (1, 2), '…of several arguments too');
    ck(g((1, 2).grep({ $_ })).WHAT, Seq, '…but a lone Seq passes through');
}

ck(Buf.new(1, 2, 3).iterator.count-only, 3, 'a Buf iterates its elements');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
