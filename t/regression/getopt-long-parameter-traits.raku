# Regression: the Getopt::Long gauntlet's last five gaps (2026-08-30) — the
# cluster that kept the ecosystem's single biggest dep-blocker red (55 dists
# name it). Every expectation is the Rakudo 2026.08 answer, taken from a
# scratch probe run against both engines.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# --- a PARAMETER's user traits reach trait_mod:<is> -------------------------
# `sub f(:$foo is option("=s%"))` calls trait_mod:<is>(Parameter, :option(…)).
# The traits were parsed and thrown away, so the handler never ran at all.
my role Fmt { has $.argument }
multi sub trait_mod:<is>(Parameter $param, Str:D :pos($argument)!) {
    $param does Fmt($argument);          # positional preset
}
multi sub trait_mod:<is>(Parameter $param, Str:D :nam($argument)!) {
    $param does Fmt(:$argument);         # NAMED preset — the spelling a
}                                        # trait_mod reaches for
multi sub trait_mod:<is>(Parameter $param, Code:D :$conv!) {
    $param does Fmt($conv);
}

sub declared($a is pos("P"), :$b is nam("N")) { }
my @p = &declared.signature.params;
ok(@p[0] ~~ Fmt && @p[0].argument eq 'P', 'a positional parameter carries its trait');
ok(@p[1] ~~ Fmt && @p[1].argument eq 'N', 'the NAMED preset form presets too');

# …and the mixin STICKS: the meta-object `.signature.params` answers with is the
# one the trait mixed into, not a copy rendered fresh from the declaration.
ok(&declared.signature.params[0].argument eq 'P', 'the mixin survives a second .params');

# An ANONYMOUS sub and a pointy block carry them as well — `sub (Str :$foo is
# option(*.flip)) {}` is handed to Getopt::Long inline, and reaches none of the
# declaration paths a named sub takes.
my $anon = sub (:$c is pos("A")) { };
ok($anon.signature.params[0].argument eq 'A', 'an anonymous sub runs parameter traits');
my $ptr = -> :$d is pos("B") { };
ok($ptr.signature.params[0].argument eq 'B', 'a pointy block runs parameter traits');
my $conv = sub (:$e is conv(*.flip)) { };
ok($conv.signature.params[0].argument.('bar') eq 'rab', 'a Code-valued trait argument arrives as Code');

# --- `does R(:name(value))` presets by NAME ---------------------------------
# The named spelling made an anonymous PARAMETERIZED role instead, so the object
# came back matching nothing.
my role R { has $.a }
class C { }
my $x = C.new; $x does R(:a(9));
ok($x ~~ R && $x.a == 9 && $x.WHAT.^name eq 'C+{R}', 'does R(:a(9)) presets the attribute');
my $y = C.new; $y does R(7);
ok($y ~~ R && $y.a == 7, 'the positional form still presets');

# --- a role mixed into a ROUTINE renames its type ---------------------------
# `.WHAT` answered the bare `Sub` although the role was there: Getopt::Long's
# `is getopt` trait tests `&main1.WHAT !=== Sub` and a name carrying the role's.
my role Parsed { has $.getopt }
multi sub trait_mod:<is>(Sub $sub, Bool :$getopt!) { $sub does Parsed(99) }
my sub marked() is getopt { }
ok(&marked.WHAT !=== Sub, 'a mixed-in routine is not quite a Sub');
ok(&marked.WHAT.^name.contains('Parsed'), '…and its type names the role');
ok(&marked.getopt == 99, '…while the accessor still answers');

# --- coercion types answer the metamodel ------------------------------------
# `Foo(Str) :$foo` reported a bare `Foo`, so a module could not tell a coercion
# from a plain type, let alone ask for its two halves.
class Foo { has Int:D $.value is required; method COERCE(Int(Str) $v) { Foo.new(:value($v)) } }
sub coercing(Foo(Str) :$foo) { }
my $ct = &coercing.signature.params[0].type;
ok($ct.^name eq 'Foo(Str)', 'a coercion parameter reports the coercion type');
ok($ct.HOW ~~ Metamodel::CoercionHOW, '…whose HOW is a CoercionHOW');
ok($ct.^constraint_type === Str, '…^constraint_type is what it parses from');
ok($ct.^target_type === Foo, '…^target_type is what it must become');
ok($ct.^coerce('1').value == 1, '…and ^coerce runs the target COERCE');

# --- `KEY => my $x` carries the CONTAINER -----------------------------------
# The out-parameter idiom: `get-options-from(@args, 'foo' => my $foo)` binds the
# caller's variable and the parse writes into it.
my $pair = ('foo' => my $out);
$pair.value = 42;
ok($out == 42, 'a pair built over a declaration writes back through .value');
my %h;
%h<foo> := $pair.value;
%h<foo> = 99;
ok($out == 99, '…and through a hash element bound to it');
# SCOPE, stated rather than asserted: Rakudo preserves the container for ANY
# variable on the right of `=>`; rakupp does it for the DECLARATION form only,
# which is where the idiom lives. `k => $x` still copies here — a difference,
# not a fix, so there is nothing to pin.
# …and it renders as what it holds, never as its own FETCH/STORE pair
my $shown = ('k' => my $held);
ok($shown.gist eq 'k => (Any)', 'a container gists as what it holds');
$held = 7;
ok($shown.gist eq 'k => 7', '…and follows it');
# a container is transparent to a type test — a bound scalar is not Associative
my $isassoc = %h<foo> ~~ Associative;
ok(!$isassoc, 'a bound scalar type-matches what it holds');

# --- a non-Positional value is its own single element -----------------------
# `/ ^ (\w+) /[0]` is that very regex — answering Any made the smartmatch
# `$key ~~ Any`, True, and Getopt::Long named every option "True".
ok((/ ^ (\w+) /[0]).WHAT === Regex, 'a Regex indexes to itself');
my ($name) = 'foo=i@' ~~ / ^ (\w+) /[0];
ok(~$name eq 'foo', '…so the smartmatch answers a Match, not a Bool');
class K { }
ok(K.new[0].WHAT === K, 'a plain object indexes to itself');
ok(42[0] == 42, '…as an Int already did');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
