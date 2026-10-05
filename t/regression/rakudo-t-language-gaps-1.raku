# Regression: language behaviour from the first batch of Rakudo t/ gap files
# (2026-10-05).
#
# - `3 ~~ $_` reads the OUTER topic on the right; `Q«a«b»c»` nests.
# - `*&code` binds one Callable and is not slurpy.
# - A Label knows its name, file and line; `$<p>:exists` asks a Match;
#   `SETTING::{…}:exists` asks the core; `CORE::<Int>` is the type.
# - `has IO::Handle $.x = Nil` holds the type object; an `@` constant keeps a
#   lazy Seq lazy; `.=` through a `take-rw` element stores.
# - `R[(a => 1)]` passes a Pair; `<( )>` in an interpolated regex marks only
#   the inner match; `rule {…}` keeps sigspace under `~~`; CR LF is one grapheme.
# - Two `will` traits on one variable both run; `%.0a` rounds half to even;
#   `my :($a) = …` is refused; an indexed format still counts its arguments.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub dies-with($code, $type) { (try { EVAL $code; 'lived' }) // $!.^name eq $type }

$_ = 5;
ck((3 ~~ $_, 5 ~~ $_), (False, True), 'a bare $_ on the right of ~~ is the outer topic');
ck(Q«a«b»c», 'a«b»c', 'guillemet quotes nest');

sub with-star($t, *&code) { code($t) }
ck(with-star(21, * * 2), 42, 'a *& parameter binds one Callable');
ck(&with-star.signature.params[1].slurpy, False, '... and is not slurpy');

FOO: for 1 { ck((FOO.name, FOO.line, FOO.file.IO.basename), ('FOO', 32, 'rakudo-t-language-gaps-1.raku'), 'a Label knows where it was written') }

my $m = "ab" ~~ /$<p>=a/;
ck(($m<p>:exists, $m<q>:exists, $m<q>:!exists), (True, False, True), ':exists on a Match');
ck((SETTING::{'&say'}:exists, SETTING::{'$?FILE'}:exists), (True, False), 'SETTING:: answers for the core');
ck(CORE::<Int>, Int, 'CORE::<Int> is the type');

class NilDefault { has IO::Handle $.x = Nil }
ck(NilDefault.new.x.^name, 'IO::Handle', 'a typed attribute defaulted to Nil holds its type object');

constant @fib = 1, 1, * + * ... *;
constant @gathered = gather { take 1; take 2 };
ck((@fib[5], @gathered.join(','), @gathered.^name), (8, '1,2', 'List'), 'an @ constant keeps a lazy Seq');

my @a = 1, 2, 3;
.=succ for gather { take-rw @a[1] }
ck(@a.List, (1, 3, 3), '.= through a take-rw element stores');

role PairParam[$x] { method v { $x } }
class TakesPair does PairParam[(a => 1)] { }
ck(TakesPair.v, (a => 1), 'a parenthesized pair parameterizes a role positionally');

my $inner = / o <( o )> b /;
ck(("foobar" ~~ / $inner /).Str, 'oob', 'capture markers of an interpolated regex stay inside it');
ck("foobar".split(/ $inner /).List, ('f', 'ar'), '... for .split too');
ck(("a b" ~~ rule { a b }).Str, 'a b', 'an anonymous rule keeps sigspace under ~~');
ck(("a\r\nb" ~~ /\r/).so, False, 'a lone \r does not split CR LF');
ck(("a\r\nb" ~~ /\r\n/).so, True, '\r\n is the CR LF grapheme');
ck(("a\r\nb" ~~ /\x0D\n/).so, False, '... a \x0D escape does not fuse');

my $trace = run($*EXECUTABLE, '-e', 'my $x will enter { say "enter" } will leave { say "leave $_" } = 7; say "body $x"', :out);
ck($trace.out.slurp(:close).lines.List, ('enter', 'body 7', 'leave 7'), 'two will traits on one variable both run');

ck((sprintf('%.0a', 1.5e0), sprintf('%.1a', 1.09375e0), sprintf('%.1a', 255.5e0)),
   ('0x2p+0', '0x1.2p+0', '0x2.0p+7'), '%a rounds half to even');
ck(dies-with('my :($a, $b) = (1, 2)', 'X::Syntax::Variable::SignatureAssignment'), True,
   'a signature literal is not assigned');
ck(dies-with('my :($a, $b)', 'X::Syntax::Variable::SignatureWithoutInitializer'), True,
   '... nor left without an initializer');
ck((try { sprintf(Q[%2$d %d %d], 1); 'lived' }) // $!.^name, 'X::Str::Sprintf::Directives::Count',
   'an indexed format still counts its unindexed directives');
ck((try { (-∞^..^∞).in-range(0/0); 'lived' }) // $!.message, 'Value out of range. Is: <0/0>, should be in -Inf^..^Inf',
   'in-range names a 0/0 that is out of range');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
