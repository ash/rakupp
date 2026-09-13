# Regression: nine faults a web framework written in Raku walked into.
#
# Six were in `--target=js` (the emitter and its runtime) and three in the
# parser and the lexer. They are collected here because they share an origin —
# building a framework whose builders are subs named after HTML tags, whose
# handlers are closures made in loops, and whose pages call routines that only
# exist on a server — and because every one of them is plain Raku that the
# interpreter has always got right. The file therefore doubles as a `--target=js`
# corpus entry: t/js/run.raku transpiles it and compares the two.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# 1. `*%a` is the slurpy NAMED hash. The emitter bound the POSITIONAL arguments
#    into it, so a builder taking `*%attrs` died on an odd count — and `f()`
#    with no arguments at all died too.
sub slurpy-named(*@pos, *%named) { "{@pos.elems}/{%named.sort.map({ .key ~ '=' ~ .value }).join(',')}" }
check slurpy-named('a', 'b', :id<x>, :class<y>), '2/class=y,id=x', 'a slurpy hash takes the named arguments';
check slurpy-named(),                            '0/',             'and an empty call binds nothing';
check slurpy-named(:only<named>),                '0/only=named',   'named arguments alone';
sub named-plus-slurpy(:$first, *%rest) { "$first|{%rest.sort.map(*.key).join(',')}" }
check named-plus-slurpy(:first<a>, :b(1), :c(2)), 'a|b,c', 'an explicit named parameter is not in the slurpy';

# 2. A declared sub whose name is a WORD INFIX wins at term position, the way it
#    does on Rakudo once the routine is in scope. `say div "x"` used to parse as
#    `say() div "x"`, which made a `<div>` builder impossible to call.
sub div(Str $t) { "div($t)" }
sub min(Str $t) { "min($t)" }
check (div 'txt'),  'div(txt)',  'a declared `div` is the sub at term position';
check (div('txt')), 'div(txt)',  'and the parenthesised call agrees';
check (7 div 2),     3,          'the infix still divides when nothing shadows it';
check (7 mod 2),     1,          '…and `mod`';
check (min 'x'),    'min(x)',    'a declared `min` is the sub';
check (Inf min 5),   5,          'but after a bare TYPE name the infix is still meant';

#    A declared sub whose name is a QUOTE keyword wins too — unless an ADVERB
#    follows, which is exactly where Rakudo draws the line.
sub tr(&b) { 'tr(' ~ b() ~ ')' }
check (tr { 'row' }), 'tr(row)', 'a declared `tr` takes a block';
my $s = 'hello';
$s ~~ s:g/l/L/;
check $s, 'heLLo', 'an adverb keeps the substitution a substitution';

# 3. `.trans` with a LIST on each side pairs whole strings, not characters. The
#    runtime mapped the CONCATENATED text position by position, so `"` came out
#    as the `p` of `&quot;` and escaped HTML was silently not escaped.
sub esc(Str $t) { $t.trans(['&', '<', '>', '"', "'"] => ['&amp;', '&lt;', '&gt;', '&quot;', '&#39;']) }
check esc(Q{a & b < c > "d" 'e'}), 'a &amp; b &lt; c &gt; &quot;d&quot; &#39;e&#39;', 'list-to-list trans';
check 'abcd'.trans(['ab', 'c'] => ['X', 'Y']), 'XYd',  'a multi-character key matches longest-first';
check 'hello'.trans('el' => 'ip'),             'hippo', 'the STRING form is still per character';
check 'xyz'.trans(['x', 'y'] => ['1']),        '11z',   'a short replacement list repeats its last';

# 4. A file-scope `constant` has to be visible inside a class method. Classes are
#    emitted first in their statement list, so the declaration had not been
#    walked when the method that names it was.
my constant VOID = <br hr img>;
class Tag {
    has Str $.name;
    method void(--> Bool) { $!name (elem) VOID }
}
check Tag.new(name => 'br').void,  True,  'a constant reaches a class method';
check Tag.new(name => 'div').void, False, '…and answers correctly for a miss';

# 5. A `&`-sigil PARAMETER is nearer than a sub of the same name. Without that,
#    `sub page($p, &body)` stored the global `body` — a tag builder — in its
#    route, and every page rendered one empty `<body>`.
sub body(|c) { 'THE-TAG-BUILDER' }
class Route { has &.body; has Str $.path }
my @routes;
sub page(Str $path, &body) {
    @routes.push: Route.new(:$path, :&body);
    body();                                  # the PARAMETER, not the sub
}
check page('/', { 'THE-BLOCK' }), 'THE-BLOCK',       'a `&` parameter shadows a sub when called';
check @routes[0].body.(),         'THE-BLOCK',       '…and when passed on as a value';
check body(),                     'THE-TAG-BUILDER', 'outside, the sub is still the sub';
my &alias = -> { 'THE-ALIAS' };
sub run-it(&f) { f() }
check run-it(&alias), 'THE-ALIAS', 'a `my &` variable behaves the same';

# 6. Two ANONYMOUS parameters in one signature. Both mangled to the same name,
#    which is a JavaScript syntax error and took the whole program with it.
sub two-anon($, $) { 'two' }
check two-anon(1, 2), 'two', 'a sub with two anonymous parameters';
my &blk = -> $, Str $ { 'block' };
check blk(1, 'x'), 'block', 'and a block with two';
my &mixed = -> $a, $, $c { "$a-$c" };
check mixed(1, 2, 3), '1-3', 'an anonymous parameter between two named ones';

# 7. A type SMILEY in a smartmatch. `$x ~~ Tag:D` was true for the type object,
#    so a guard meant to catch "nothing here" let it through.
my $instance = Tag.new(name => 'p');
check ($instance ~~ Tag:D), True,  'an instance is Tag:D';
check (Tag      ~~ Tag:D), False, 'a type object is NOT Tag:D';
check (Tag      ~~ Tag:U), True,  'a type object is Tag:U';
check ($instance ~~ Tag:U), False, 'an instance is not Tag:U';
check (Tag      ~~ Tag),   True,  'and the plain form matches either';
check ((1, 2, 3).all ~~ Int:D), True, 'a junction collapses through the smiley';

# 8. Every closure made in a `for` loop kept its OWN loop variable. They shared
#    one, and saw its last value, so three rows of a list all called the handler
#    of the third.
{
    my @handlers;
    for 1..3 -> $i { @handlers.push({ "i=$i" }) }
    check @handlers.map({ .() }).join(' '), 'i=1 i=2 i=3', 'a closure per iteration';

    my @kv;
    for <a b c>.kv -> $k, $v { @kv.push({ "$k:$v" }) }
    check @kv.map({ .() }).join(' '), '0:a 1:b 2:c', '…with two loop variables';

    my @topics;
    for 1..3 { @topics.push({ "t=$_" }) }
    check @topics.map({ .() }).join(' '), 't=1 t=2 t=3', '…and over the bare topic';

    # the LAST phaser still reads the last value, which is why the variable used
    # to live outside the loop in the first place
    my @both;
    my $last = '';
    for 1..3 -> $j { @both.push({ $j }); LAST $last = "last=$j" }
    check @both.map({ .() }).join(','), '1,2,3', 'closures and a LAST phaser together';
    check $last, 'last=3', 'and the phaser sees the final value';
}

# 9. `* => True` as an argument curries over the WHOLE pair. It passed a Pair
#    with a literal Whatever key instead, so `map` was handed a Pair where it
#    wanted something callable.
{
    my @words = <a b c>;
    my %seen = @words.map(* => True);
    check %seen.keys.sort.join(','), 'a,b,c', 'a curried pair as a map argument';
    check %seen<b>, True, '…and its values';
    # an ordinary pair argument is NOT a curry
    sub takes-pair($p) { "{$p.key}={$p.value}" }
    check takes-pair('k' => 1), 'k=1', 'a plain pair argument stays a pair';
    sub takes-named(:$x) { "x=$x" }
    check takes-named(x => 2), 'x=2', 'and a named argument stays named';
}

say @fail ?? "FAIL: {@fail.join('; ')}" !! 'PASS';
exit @fail ?? 1 !! 0;
