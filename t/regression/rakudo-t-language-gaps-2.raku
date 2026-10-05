# Regression: language behaviour from the second batch of Rakudo t/ gap files
# (2026-10-05).
#
# - `for $slip` iterates a scalar-held Slip; CR LF is a member of a character
#   class only through its flags (`<[\n]>`, `<-[\v]>`), not `<[\v]>` or
#   `<[\x0A]>`; a standalone `\v` still takes it.
# - Hashes `cmp` as their sorted pairs; an endless Seq `cmp` a finite list is
#   read only as far as needed.
# - `but` is a structural infix (`48 but 1 + 2`); `but R<a b>` hands over the
#   list; `5 but Any` is refused.
# - A bare block's signature and `.cando`; `&?BLOCK` in a `with` block.
# - `try` fatalizes method calls too, and `no fatal` in it keeps the Failure.
# - `token t:sym(EXPR)` and `method t:sym(EXPR)`; a `rule` quantifier gives
#   text back across the whitespace after it; `QUIT` needs a block.
# - `$0:exists`, `$<a>:kv`; `$*PACKAGE` in a role body and in a package's own
#   trait.
# - 6.c's empty `$()`/`@()`/`%()`, and 6.e's `next`/`last` payloads.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub lines-of(*@code) { run($*EXECUTABLE, '-e', @code.join("\n"), :out).out.slurp(:close).lines.List }

my $slip = slip(2, 19, 7);
my @got;
@got.push($_) for $slip;
ck(@got.List, (2, 19, 7), 'for flattens a scalar-held Slip');

my $crlf = "\r\n";
ck((so $crlf ~~ /^<[\v]>$/, so $crlf ~~ /^<-[\v]>$/, so $crlf ~~ /^<[\n]>$/, so $crlf ~~ /^<[\x0A]>$/,
    so $crlf ~~ /^\v$/, so $crlf ~~ /^<[\H]>$/), (False, True, True, False, True, True),
   'CR LF in character classes');

ck(({:a(10)} cmp {:a(9)}, {:b(1)} cmp {:a(1), :b(1)}), (More, More), 'Hashes cmp as sorted pairs');
ck(((1..Inf).Seq cmp (1, 2, 3), (1, 2) cmp (1..Inf).Seq), (More, Less), 'an endless Seq in cmp');

my role Typed { has $.type }
ck((48 but 1 + 2).Int, 3, 'but is looser than +');
ck((45 but Typed<a b>).type, $("a", "b"), 'but R<a b> passes the list');
ck((try { my $m = 50 but Any; 'lived' }) // $!.^name, 'X::Mixin::NotComposable', 'but Any is refused');

ck({ $_ }.signature.raku, ':(;; $_? is raw = OUTER::<$_>)', "a bare block's signature");
ck(({ $_ }.cando(\("x")).elems, { $_ }.cando(\(1, 2)).elems), (1, 0), '... and its cando');
with 5 { ck(&?BLOCK.^name, 'Block', '&?BLOCK in a with block') }

sub f($v) { try { return $v.Int }; 'fell' }
ck(f('abc'), 'fell', 'a Failure from a method call in try throws');
ck((try { no fatal; 'abc'.Int }).^name, 'Failure', '... unless the block says no fatal');

my enum Opt (MARK => 'Q');
grammar G {
    proto token tok {*}
    token tok:sym(MARK)     { <sym> . }
    token tok:sym("a" ~ "b") { <sym> }
    token tok:sym<x>        { . }
}
class GA { method tok:sym(MARK)($/) { make 'mark' } }
ck(G.parse('QZ', :rule<tok>, :actions(GA)).made, 'mark', ':sym(EXPR) names a candidate and its action');
ck(G.parse('ab', :rule<tok>).Str, 'ab', '... a folded expression too');

grammar R {
    token word { \w+ }
    token val  { 'FALSE' }
    rule  opt  { <word>? <val> }
    rule  rat  { <word>?: <val> }
}
ck((so R.parse('FALSE', :rule<opt>), so R.parse('FALSE', :rule<rat>)), (True, False),
   'a rule quantifier backtracks across its whitespace');
ck((try { EVAL 'react { QUIT say 1; whenever Supply.from-list(1) { } }'; 'lived' }) // $!.^name,
   'X::Syntax::Missing', 'QUIT needs a block');

"ab" ~~ /(.)(.)/;
ck((($0:exists), ($2:exists), ($0:k)), (True, False, 0), 'subscript adverbs on $0');
"xy" ~~ /$<a>=(.)/;
ck((($<a>:kv)[0], ($<m>:exists)), ('a', False), '... and on $<a>');

multi trait_mod:<is>(Attribute:D $a, :$kind!) {
    my $k = $*PACKAGE.HOW ~~ Metamodel::ParametricRoleHOW ?? 'role' !! 'class';
    $*PACKAGE.^add_method('how-kind', my method () { $k });
}
role InRole { has $.y is kind }
class DoesIt does InRole { }
ck(DoesIt.how-kind, 'role', "a role body's \$*PACKAGE is the role");
role Marked { }
multi trait_mod:<is>(Mu:U \type, :$selfmark!) { $*PACKAGE.HOW does Marked unless $*PACKAGE.HOW ~~ Marked }
class Outer { class Inner is selfmark { } }
ck((Outer::Inner.HOW ~~ Marked, Outer.HOW ~~ Marked), (True, False), "a package trait's \$*PACKAGE");

ck(lines-of('use v6.c;', '"abc" ~~ /b { make 9 }/;', 'say $();', '"ab" ~~ /(a)(b)/;', 'say @().elems'),
   ('9', '2'), "6.c's empty contextualizers read \$/");
ck(lines-of('use v6.e.PREVIEW;', 'say (1..4).map({ next slip($_, -$_) if $_ %% 2; $_ });',
            'say (1..4).map({ last 9 if $_ == 3; $_ });', 'say (do for 1..3 { next 0 if $_ == 2; $_ })'),
   ('(1 2 -2 3 4 -4)', '(1 2 9)', '(1 0 3)'), "6.e's next/last payloads");

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
