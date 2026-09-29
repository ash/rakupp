# Regression: `<alias=rule>` is ONE capture that answers to two keys, $<alias>
# and $<rule> (Rakudo's reading, adopted in d47c8f84 for YAMLish's directives
# action). The matcher files a record under each key, and the Match builder
# built each record as if it were a capture of its own: the rule's action fired
# once PER KEY, `$<v> === $<rule>` was False, and with aliases nested every
# subtree was rebuilt 2^depth times. YAMLish nests them at every level
# (`<value=block(…)>` → `<element=cuddly-list-entry(…)>` → `<value=…>`), so a
# four-level list took 70 s where Rakudo takes 0.02 s, and the Raku course's
# 2,653-line table of contents did not finish. The records now share one
# ParseNode::aliasId, and the builder makes one Match for all of them.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use lib $?FILE.IO.parent.add('../fixtures/yamlish/lib');
use YAMLish;

my @fail;

# 1. one capture, two keys: one action, one object (oracle: Rakudo 2026.08)
grammar G {
    token TOP { <v=foo> <w=.bar> }
    token foo { 'x' <inner=baz> }
    token bar { 'y' }
    token baz { 'z' }
}
class Count {
    has %.n;
    method TOP($/) { %!n<TOP>++ }
    method foo($/) { %!n<foo>++; make 42 }
    method bar($/) { %!n<bar>++ }
    method baz($/) { %!n<baz>++; make 7 }
}
my $count = Count.new;
my $m = G.parse('xzy', :actions($count));
my $fired = $count.n.sort.map({ .key ~ '=' ~ .value }).join(',');
@fail.push("actions fired $fired") unless $fired eq 'TOP=1,bar=1,baz=1,foo=1';
@fail.push("keys {$m.hash.keys.sort.join(',')}") unless $m.hash.keys.sort.join(',') eq 'foo,v,w';
@fail.push('$<v> is not $<foo>') unless $m<v> === $m<foo>;
@fail.push('$<v><inner> is not $<foo><baz>') unless $m<v><inner> === $m<foo><baz>;
@fail.push("made {$m<v>.made // 'Nil'} / {$m<foo>.made // 'Nil'}")
    unless ($m<v>.made // 0) == 42 && ($m<foo>.made // 0) == 42;

# 2. nested: one firing per level. Built per key it was 2^(depth+1) - 2.
grammar Nest {
    token TOP  { <v=nest> }
    token nest { '(' [ <v=nest> | 'x' ] ')' }
}
class Levels { has $.n = 0; method nest($/) { $!n++ } }
my $depth = 12;
my $levels = Levels.new;
Nest.parse('(' x $depth ~ 'x' ~ ')' x $depth, :actions($levels)) or @fail.push('Nest did not parse');
@fail.push("nest fired {$levels.n} times for $depth levels") unless $levels.n == $depth;

# 3. the real shape: YAMLish on a list nested four deep. The bound is a
# blow-up detector, not a benchmark: linear is milliseconds, 2^depth was 70 s.
my $yaml = '';
my $ind = '';
for ^4 -> $d {
    $yaml ~= "{$ind}- title: t$d\n{$ind}  url: u$d\n{$ind}  items:\n";
    $ind ~= '    ';
}
$yaml ~= "{$ind}- title: leaf\n";
my $t0 = now;
my @docs = load-yamls($yaml);
my $secs = now - $t0;
@fail.push("nested YAML too slow: {$secs.round(0.1)} s") if $secs > 20;
# a canonical text of the parsed data, so the check does not lean on eqv
sub canon($x) {
    $x ~~ Associative ?? '{' ~ $x.keys.sort.map({ "$_=" ~ canon($x{$_}) }).join(',') ~ '}'
        !! $x ~~ Positional ?? '[' ~ $x.map(&canon).join(',') ~ ']'
        !! ~$x
}
my $want = '[{title=leaf}]';
$want = "[\{items=$want,title=t$_,url=u$_\}]" for (^4).reverse;
my $got = @docs.elems == 1 ?? canon(@docs[0]) !! "{@docs.elems} documents";
@fail.push("nested YAML parsed as $got") unless $got eq $want;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
