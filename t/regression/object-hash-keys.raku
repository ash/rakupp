# Regression: an OBJECT hash (`my %h{Int}`) keeps its KEY TYPE.
#
# The store is a map<std::string, Value>, so a key is a lookup STRING and its
# original value has to come from somewhere else. A Set/Bag/Mix parks it in the
# count's pairKey; an object hash rebuilds it from the declared key type; a plain
# hash genuinely keys on Str. Before `hashEntryKey` there was no single answer:
# the ternary was spelled out at some sites, missing at most, and the object hash
# had nothing at all — so `%h{+$k} = 66` came back as the Str "33" and `.raku`
# printed a plain `{"33" => 66}` (github.com/ash/rakupp/issues/9).
#
# Still open, deliberately: a key type that cannot be rebuilt from a string — a
# class, or bare Any/Mu, where Rakudo distinguishes `%h{3}` from `%h<3>` and we
# cannot — stays a Str. That is the pre-existing "Hash keys are plain strings"
# limit, narrowed rather than guessed at.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

my %h{Int};
%h{3} = 'a';
my $k = "33";
%h{+$k} = 66;

check(%h.raku, '(my Any %{Int} = 3 => "a", 33 => 66)', '.raku renders the declaration');
check(%h.keys.sort.raku,         '(3, 33).Seq',              'keys are Ints, not Strs');
check(%h.keys.sort[0].WHAT.gist, '(Int)',                    'and report Int');
check(%h.pairs.sort.raku,        '(3 => "a", 33 => 66).Seq', '.pairs keys are Ints');
# .kv is a flat k,v,k,v list in HASH order, which Rakudo randomizes per process —
# so assert what the fix is about (the keys come back numeric) without the order
check(%h.kv.grep(* ~~ Int).sort.raku, '(3, 33, 66).Seq', '.kv keys are Ints');
check(%h.kv.elems.gist,               '4',               'and .kv is still flat');
check(%h.antipairs.sort.raku,    '(66 => 33, :a(3)).Seq',    '.antipairs values are Ints');
check(%h.invert.sort.raku,       '(66 => 33, :a(3)).Seq',    '.invert values are Ints');
check(%h.sort.raku,              '(3 => "a", 33 => 66).Seq', 'sorting uses the same keys');
check(%h{33}.gist,   '66', 'lookup still finds the entry');
check(%h.elems.gist, '2',  'and there are two of them');
check(%h.^name, 'Hash[Any,Int]', 'the type name carries the parameters');

my %e{Int};
check(%e.raku,  '(my Any %{Int})', 'an empty object hash still shows its constraint');
check(%e.^name, 'Hash[Any,Int]',   'and its name');

my Int %t{Str};
%t<a> = 1;
check(%t.raku,  '(my Int %{Str} = :a(1))', 'a Str-keyed object hash keeps the :ident form');
check(%t.^name, 'Hash[Int,Str]',           'both parameters appear');
check(%t.keys[0].WHAT.gist, '(Str)',       'a Str key stays a Str');

# a plain hash must be untouched — this is the hot path for every hash iteration
my %p;
%p<x> = 1;
check(%p.raku,  '{:x(1)}', 'a plain hash is unaffected');
check(%p.^name, 'Hash',    'and keeps its bare name');
check(%p.keys[0].WHAT.gist, '(Str)', 'with Str keys');

# the Set/Bag path that already used pairKey must still work
check(set(1, 2).keys.sort.raku,  '(1, 2).Seq',       'a Set still recovers elements from pairKey');
check(bag(1, 1, 2).kv.sort.raku, '(1, 1, 2, 2).Seq', 'and a Bag its counts');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
