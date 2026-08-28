# Regression: the antononcube batch (2026-08-28) — `is-deeply` becoming `eqv`,
# and the twelve engine bugs that the honest comparison then exposed. Every
# expectation below was read off RAKUDO first, not derived from rakupp.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# ---- is-deeply IS eqv, and eqv is type-aware for hashes -------------------
# (probed as strings so a failure names the shape, not just False)
check((try { "11" eqv 11 }),                False, 'Str eqv Int');
check((try { 1 eqv 1.0 }),                  False, 'Int eqv Rat');
check((try { (1,2) eqv [1,2] }),            False, 'List eqv Array');
check((try { {a=>1} eqv Map.new((a=>1)) }), False, 'Hash eqv Map');
check((try { set(1,2) eqv SetHash.new(1,2) }), False, 'Set eqv SetHash');
check((try { (my Int %t = (a=>1)) eqv {a=>1} }), False, 'Hash[Int] eqv Hash');
check((try { mix(1,2) eqv bag(1,2) }),      False, 'Mix eqv Bag');
check((try { (1,2).Seq eqv (1,2) }),        False, 'Seq eqv List');
# …but is-deeply CACHES a Seq operand, so this one passes
{
    my $tmp = $*TMPDIR.add("rk-isdeeply-{$*PID}.out");
    my $p = run($*EXECUTABLE, '-e',
        'use Test; is-deeply (1,2).Seq, (1,2); is-deeply "11", 11; done-testing;',
        :out, :err);
    my $out = $p.out.slurp(:close); $p.err.slurp(:close);
    check($out.lines.grep(*.starts-with('ok 1')).elems,     1, 'is-deeply caches a Seq');
    check($out.lines.grep(*.starts-with('not ok 2')).elems, 1, 'is-deeply is type-strict');
    $tmp.unlink if $tmp.e;
}

# ---- the hyper result's SHAPE --------------------------------------------
my @a = 1, 2;
check((3 <<+>> @a).WHAT.^name,        'Array', 'scalar <<+>> Array is an Array');
check((3 <<+>> (1,2)).WHAT.^name,     'List',  'scalar <<+>> List is a List');
check(((1,2) >>+>> @a).WHAT.^name,    'List',  'List >>+>> Array is a List');
{
    my Cool %c = (a => 1); my Cool %d = (a => 2);
    check((%c >>~<< %d).WHAT.^name, 'Hash[Cool]', 'a hyper keeps the hash type');
    my %o{Any} = (a => 1);
    check((%o >>~<< %o).WHAT.^name, 'Hash[Any,Any]', 'a hyper keeps object-keyedness');
}
# nodal vs deep: `.elems` asks about the node, `.Str` reaches the leaves
check(([[1,2],[3,4]]>>.elems).raku, '(2, 2)',                'a nodal method does not descend');
check(([[1,2],[3,4]]>>.Str).raku,   '[["1", "2"], ["3", "4"]]', 'a non-nodal method reaches the leaves');
check(({a=>1,b=>2}>>.Str).raku,     '{:a("1"), :b("2")}',    'a hyper over a Hash keeps its keys');

# ---- set operators keep the mutable flavour -------------------------------
{
    my $sh = SetHash.new(<a b>); my $s = Set.new(<a b>); my $bh = BagHash.new(<a b>);
    check(($sh (|) $s).^name,  'SetHash', 'SetHash (|) Set is a SetHash');
    check(($s (|) $sh).^name,  'Set',     'Set (|) SetHash is a Set');
    check(($sh (^) <a b>).^name, 'Set',   '(^) needs both sides QuantHash');
    check(($sh (+) $sh).^name, 'BagHash', 'SetHash (+) SetHash is a BagHash');
    check(($bh (|) $sh).^name, 'BagHash', 'the left operand decides');
    check(infix:<(-)>($sh.item).^name, 'Set', 'the one-operand (-) is immutable');
    check(infix:<(|)>($sh.item).^name, 'SetHash', 'the one-operand (|) keeps it');
}

# ---- `is copy` in a `for` signature ---------------------------------------
{
    my %h = a => (1, 2, 3);
    my $kind = '';
    for %h.kv -> $k, @v is copy { $kind = @v.WHAT.^name }
    check($kind, 'Array', 'a for-loop @param with `is copy` is a fresh Array');
    my $plain = '';
    for %h.kv -> $k, @v { $plain = @v.WHAT.^name }
    check($plain, 'List', '…and without it the argument is unchanged');
}

# ---- Any's one-element-list interface on an object ------------------------
{
    class Widget { has $.x }
    my $w = Widget.new(x => 1);
    check($w.elems, 1,       'an object has .elems 1');
    check($w.end,   0,       'an object has .end 0');
    check($w.keys.raku, '(0,).Seq', 'an object has .keys (0)');
    check(($w.head === $w), True, 'an object is its own .head');
}

# ---- .^find_method sees an attribute accessor; a type object is probed -----
{
    class Node { has $.type }
    check((Node.new(type => 1).^find_method('type') // Nil).defined, True,
          '^find_method sees an attribute accessor');
    check((Int.^find_method('type') // Nil).defined, False,
          '^find_method on a builtin type object is honest');
    check(Str.^lookup('parse-base').^name, 'Method',
          '…and still finds a real method that needs an argument');
}

# ---- a Whatever-curried SLICE ---------------------------------------------
{
    my %r = A => 1, B => 2, C => 3;
    check(((%r, %r).map(*<A B>)).raku, '((1, 2), (1, 2)).Seq', '*<A B> slices');
    my @m = [10, 20, 30];
    check((([@m, @m]).map(*[0,1])).raku, '((10, 20), (10, 20)).Seq', '*[0,1] slices');
}

# ---- Nil RESETS an attribute to its default -------------------------------
{
    class Slot { has $.t; has Int $.n }
    check(Slot.new(t => Nil, n => Nil).t.raku, 'Any', 'Nil resets an untyped attribute');
    check(Slot.new(t => Nil, n => Nil).n.raku, 'Int', '…and a typed one to its type');
    role Held { has $.type; submethod BUILD(:$!type = Any) {} }
    class Box does Held { }
    check(Box.new(type => Nil).type.raku, 'Any', 'Nil resets through an attributive param');
}

# ---- `.=` on a list target, and the unspace ------------------------------
{
    my ($x, $y) = 'one', 'two';
    ($x, $y) .= reverse;
    check("$x/$y", 'two/one', '`.=` assigns across a parenthesised list');
    my @row = 5, 6, 7;
    check(@row\[1], 6, 'a zero-width unspace before a subscript');
    my @rep = <b b b e b b b>;
    check((@rep .= repeated).WHAT.^name, 'Array', '`.=` stores the sigil\'s container');
    check(@rep.WHAT.^name, 'Array', '…and leaves one behind');
}

# ---- `C but R` on a bare type name, and a grammar role mixed in at runtime -
{
    role Marked { method mark { 'marked' } }
    class Plain { }
    check((Plain but Marked).mark, 'marked', '`C but R` parses on a bare type name');
    check((Plain but Marked).DEFINITE, False, '…and answers a TYPE object');
    role Grammatical { token TOP { 'x' } }
    grammar Empty { }
    my %by-name = only => Grammatical;
    check(((Empty but %by-name<only>).parse('x') // Nil).Str, 'x',
          'a role mixed in at runtime brings its grammar rules');
}

# ---- LTM ties: within a grammar the first wins, across inheritance the derived
{
    role RBase { proto token t {*}; token t:sym<A> { 'x' } }
    role RDeriv does RBase { token t:sym<B> { 'x' } }
    grammar Composed does RDeriv { }
    class Acts { method t:sym<A>($/) { make 'A' }; method t:sym<B>($/) { make 'B' } }
    check(Composed.parse('x', rule => 't', actions => Acts.new).made, 'B',
          'the derived proto candidate wins an LTM tie');
    grammar Both { proto token u {*}; token u:sym<A> { 'x' }; token u:sym<B> { 'x' } }
    class ActsU { method u:sym<A>($/) { make 'A' }; method u:sym<B>($/) { make 'B' } }
    check(Both.parse('x', rule => 'u', actions => ActsU.new).made, 'A',
          '…and within one grammar the first declared does');
}

# ---- a code assertion in a grammar rule sees its POSITIONAL captures -------
{
    grammar Gate { rule only-ba { (\w+) <?{ $0.Str eq 'ba' }> } }
    check((Gate.parse('ba', rule => 'only-ba') // Nil).defined, True,  '$0 in a grammar assertion');
    check((Gate.parse('zz', rule => 'only-ba') // Nil).defined, False, '…and it really gates');
}

# ---- IO: .tell, warn through $*ERR, a type-object $*OUT, open's adverbs ----
{
    my $f = $*TMPDIR.add("rk-tell-{$*PID}.txt");
    my $h = open $f, :w;
    $h.print('hello');
    check($h.tell, 5, 'a write handle tells its buffer size');
    $h.close;
    my $r = open $f, :r;
    $r.read(2);
    check($r.tell, 2, 'a read handle tells its byte cursor');
    $r.close;
    $f.unlink;

    my $touch = $*TMPDIR.add("rk-touch-{$*PID}.txt");
    $touch.unlink if $touch.e;
    $touch.open(:mode<wo>, :create).close;
    check($touch.e, True, '`open(:mode<wo>, :create)` creates the file');
    check($touch.z, True, '…empty');
    $touch.unlink;
}
{
    my $captured = '';
    my $p = run($*EXECUTABLE, '-e',
        'my $b = ""; my $*ERR = class { method print(*@a) { $b ~= @a.join }; method flush {} };
         warn "through-err"; note "also-err"; $*ERR = Nil; say $b.lines.join("|");',
        :out, :err);
    $captured = $p.out.slurp(:close).trim; $p.err.slurp(:close);
    check($captured.contains('through-err'), True, 'warn goes through $*ERR');
    check($captured.contains('also-err'),    True, '…as note already did');
}

say @fail ?? "FAILED:\n" ~ @fail.join("\n") !! 'PASS';
exit @fail ?? 1 !! 0;
