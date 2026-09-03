# Regression: the two engine faults the 2026-09-03 top-100 battery refresh
# found — both of them dists that USED to pass and had stopped
# (docs/dev/ecosystem/ECOSYSTEM-TOP100.md, the 2026-09-03 sitting).
# Every expectation is the Rakudo 2026.08 answer, taken from probes run
# against both engines.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }

# --- URI (#8, 9 dists behind it): Nil through an `is rw` accessor RESETS -------
# `$obj.attr = Nil` empties the attribute to its default, the way `$x = Nil`,
# `@a[0] = Nil` and `$!attr = Nil` already did — Nil is the reset value, never
# a stored one, and a reset is not an assignment, so neither the declared type
# nor a `where` clause is consulted. URI's `.port = Nil` (`has Port $.port is
# rw` over `subset Port of UInt`) died "expected URI::Port but got Nil".
our subset Port of UInt;
class Authority {
    has Port $.port is rw;
    has Str $.userinfo is rw;
    has Numeric $.lat is rw where { -90 <= $_ <= 90 };
}
my $a = Authority.new(port => 4321, userinfo => 'me', lat => 10);
$a.port = Nil;
ok($a.port.raku eq 'Port', 'a subset-typed rw attribute resets to its type object');
ok(!$a.port.defined, '…and is undefined after the reset');
$a.userinfo = Nil;
ok($a.userinfo.raku eq 'Str', 'a Str rw attribute resets to (Str)');
$a.lat = Nil;
# (Rakudo names the reset value after the anonymous subset the `where` makes,
# rakupp after the base type; what both agree on — and what the constraint
# would have refused — is that the attribute comes back UNDEFINED.)
ok(!$a.lat.defined, 'a `where`-constrained attribute resets without running the constraint');
$a.port = 1234;
ok($a.port == 1234, 'and the attribute still takes an ordinary value afterwards');
class Untyped { has $.x is rw }
my $u = Untyped.new(x => 5);
$u.x = Nil;
ok($u.x.raku eq 'Any', 'an untyped rw attribute resets to (Any)');
ok(!(try { my Authority $b .= new; $b.port = 'str'; True }), 'a real type error still throws');

# --- Log::Async (#31): the statement line is PER THREAD ------------------------
# `callframe(N)` reports the line the call was written on. The line of the
# statement now executing is one process-wide slot while only the mainline runs
# Raku code; the moment a worker exists, that slot is written by both threads
# and a frame taken on the mainline can carry the worker's line. Log::Async
# stamps every message with `callframe(1)`, and its t/14-frame failed 7 runs
# in 20 that way — the values were right, the line numbers came from the
# logging worker.
sub where-am-i() { callframe(1) }
my @seen;
{
    my $stop = False;
    my $noise = start { my $n = 0; until $stop { my $x = 1; $x++; $n += $x }; $n };  # a worker writing lines
    for ^200 {
        @seen.push: where-am-i().line;                  # line 55
    }
    $stop = True;
    await $noise;
}
ok(@seen.unique.elems == 1, "callframe's line is stable while a worker runs (saw {@seen.unique.sort.join(',')})");
ok(@seen[0] == 55, 'and it is the line the call was written on');

sub outer() { inner() }                                 # line 63
sub inner() { callframe(1).line }
ok(outer() == 63, 'callframe(1) from a nested sub names its caller line');

# --- PDF (#79, 3 dists behind it): a subset over a DEFINITE base type ---------
# `subset S of Str:D` — the smiley belongs to the base type. Unconsumed it fell
# out of the declaration as a statement of its own ("Useless use of :D(True) in
# sink context") and took the following `is export(…)` with it, which then read
# as a call to a routine named `export`. PDF::COS declares three subsets that way.
my subset LatinStr of Str:D is export(:LatinStr) where !.contains('x');
ok('abc' ~~ LatinStr, 'a subset of Str:D accepts a definite string');
ok(!(Str ~~ LatinStr), '…and refuses the type object');
ok(!('axc' ~~ LatinStr), '…and still applies its own where clause');
my subset Undef of Int:U;
ok(Int ~~ Undef, 'a subset of Int:U accepts the type object');
ok(!(3 ~~ Undef), '…and refuses a definite value');

# --- CSS::Writer (4 dists behind it): only a BLOCK's `}` ends the statement ----
# A `}` at end of line ends the statement, so a modifier on the next line
# attaches to nothing — but a SUBSCRIPT's `}` is not a block's, and
# `return %h{$k}` / `if %h{$k}:exists;` on the next line lost its modifier and
# read the `if` as a fresh block-if ("expected { (got ';')").
my %tbl = (1 => 'a');
sub look-up($k) {
    return %tbl{ $k}
        if %tbl{ $k }:exists;
    'missing'
}
ok(look-up(1) eq 'a', 'a modifier on the next line attaches after a subscript');
ok(look-up(2) eq 'missing', '…and the modifier really gates the return');
my @items = 1, 2, 3;
sub second() {
    return @items[1]
        if @items.elems > 1;
    'short'
}
ok(second() == 2, '…the same for a positional subscript');
my $blocked = 0;
{
    my %m = (a => { 1 });
    my $v = %m<a>
        if True;
    $blocked = 1 if $v;
}
ok($blocked == 1, 'a hash whose VALUE is a block still takes the modifier');

# --- PDF::COS::Tie: a signature literal where a declaration list goes ----------
my :($sigil, $type) := ('$', 'Int');
ok($sigil eq '$' && $type eq 'Int', 'my :($a, $b) := … destructures like my ($a, $b)');
# …and a slot the right-hand list does not reach takes its DEFAULT: PDF::COS::Tie
# binds a two-element `(obj-num, gen-num)` into a three-parameter signature.
my $reader = 'the-reader';
my :(Int $obj-num, Int $gen-num, $r = $reader) := (12, 0);
ok($obj-num == 12 && $gen-num == 0 && $r eq 'the-reader',
   'a signature-literal slot past the end of the list takes its default');
my :($x, $y = 'unused') := ('a', 'b');
ok($y eq 'b', '…and a supplied value beats the default');

# --- CSS::Writer: `~$obj` runs `method Str` THROUGH the chain ------------------
# A user Str that defers to the built-in with `nextsame` worked when called as
# `$obj.Str` and died "nextsame is not in the dynamic scope of a dispatcher"
# through stringification, which reached the method without a dispatcher.
class Deferring {
    has $.ast;
    method Str { with $.ast { "wrote $_" } else { nextsame } }
}
ok((~Deferring.new(ast => 'x')) eq 'wrote x', 'a user Str runs for ~$obj');
my $fell-through = ~Deferring.new;
ok($fell-through.starts-with('Deferring'), '…and its nextsame reaches the built-in');
ok("{Deferring.new(ast => 'y')}" eq 'wrote y', '…in interpolation too');

say $fails ?? "FAIL ($fails)" !! "PASS";
