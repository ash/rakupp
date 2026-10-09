# Regression: the sixth Rakuglaze round — eleven snippets from Haiku batches
# 04 to 12 and one hand-written from HTTP::UserAgent.
#
# A declared routine takes a spaced `+`/`-` as a prefix on its argument;
# `$*name` interpolates in a regex and in a grammar token; a Str has no `.CWD`;
# deepmap over an Array assigns its leaves, so a Nil leaf is Any; a nested
# `[…]` spreads under the one-arg rule; a type named `Req` is no reversed `eq`;
# `.^nominalize` unwraps a coercion, a definite type and a subset; `|` slips a
# `$`-held hash and `.Hash` drops its `$`; `Exception.fail` makes its caller
# return a Failure; an `our sub` in a class stays in that class; `.share` on a
# supply block taps it live.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

# a declared routine before a spaced prefix operator
{
    sub g(*@a) { @a.join('|') }
    ck (g + (2 + 3) * 2), '10', 'g + (…) is g(+(…))';
    ck (g - 5), '-5', 'g - 5 is g(-5)';
}

# dynamic variables in a regex
{
    my $*LEFT = '{{';
    my $*RIGHT = '}}';
    my $m = '{{> name }}' ~~ /$*LEFT '>' \h* (\N*?) \h* $*RIGHT/;
    ck ~($m[0] // ''), 'name', '$*LEFT and $*RIGHT in a regex';
    my $*DOT = 'a.b';
    ck so('axb' ~~ /^ $*DOT $/), False, 'a $*name value matches literally';
    grammar G6 {
        token TOP { :my $*Q = '"'; <str> }
        token str { $*Q \w+ $*Q }
    }
    ck so(G6.parse('"abc"')), True, 'a :my $*Q read in another token';
}

# a Str has no CWD; an IO::Path has
{
    ck 'archive-root'.?CWD, Nil, 'Str.?CWD is Nil';
    ck 'x'.IO.CWD ~~ Str, True, 'IO::Path.CWD';
}

# deepmap over an Array assigns its leaves
{
    ck [1, 2].deepmap({ Nil }), [Any, Any], 'a Nil leaf in an Array is Any';
    ck (1, 2).deepmap({ Nil }), (Nil, Nil), 'a Nil leaf in a List stays Nil';
    my @tbl = [1, 'NA'], ['-', 3];
    @tbl = @tbl.deepmap({ $_ eq 'NA' | '-' ?? Nil !! $_ });
    ck @tbl.raku, '[[1, Any], [Any, 3]]', 'deepmap over nested Arrays';
}

# the one-arg rule spreads a nested `[…]`
{
    ck [[1]], [1], '[[1]] is [1]';
    ck [[]], [], '[[]] is []';
    ck [[1],].elems, 1, '[[1],] keeps the inner array';
    my @a = [[1, 2]];
    ck @a.elems, 2, 'my @a = [[1, 2]] has two elements';
    my class Table6 {
        has @!iterator = [[]];
        method add-hline($hline) { @!iterator[0].push($hline); @!iterator[0] }
    }
    ck Table6.new.add-hline('---').raku, '$["---"]', 'an autovivified slot comes back itemized';
}

# a declared type named like a reversed infix
{
    my class Req { has %.env; method server { $.env<SERVER_NAME> } }
    ck Req.new(env => { SERVER_NAME => 'localhost' }).server, 'localhost', 'a class named Req';
}

# .^nominalize
{
    my $default = Int(Str);
    $default := $default.^nominalize if $default.HOW.archetypes.nominalizable;
    ck $default.^name, 'Int', 'Int(Str).^nominalize';
    ck Int:D.^nominalize.^name, 'Int', 'Int:D.^nominalize';
    my subset Small6 of Int where * < 10;
    ck Small6.^nominalize.^name, 'Int', 'a subset nominalizes to its base';
}

# `|` on a `$`-held hash, and .Hash out of its Scalar
{
    my $h = {a => 1};
    ck (|$h).raku, 'slip(:a(1),)', '|$h slips the pairs';
    ck $h.Hash.raku, '{:a(1)}', '$h.Hash has no $';
    my %for-json = config => %(a => 1);
    my %config;
    given %for-json<config> {
        %config = :enums-as-value, (|.Hash with %for-json<config>);
    }
    ck %config, %(a => 1, enums-as-value => True), '(|.Hash with …) in a hash assignment';
}

# Exception.fail returns a Failure from the routine that calls it
{
    sub ef { X::AdHoc.new(payload => 'boom').fail; 'after' }
    my $r = ef();
    ck $r.defined, False, 'Exception.fail: the caller returns a Failure';
    ck $r.exception.message, 'boom', '…carrying the exception';
}

# an `our sub` belongs to its class
{
    my class UA6 {
        method get($target) { "GET $target" }
        our sub get($target where Str) { UA6.new.get($target) }
    }
    my class Fetcher6 { our sub get($target where Str) { "fetch $target" } }
    ck UA6::get('a'), 'GET a', 'our sub get in one class';
    ck Fetcher6::get('b'), 'fetch b', 'our sub get in another';
}

# .share taps a supply block live
{
    my $taps = 0;
    my $events = Supplier.new;
    my $shared = supply { $taps++; whenever $events.Supply { emit "p/$_" } }.share;
    my (@a, @b);
    $shared.tap({ @a.push: $_ });
    $shared.tap({ @b.push: $_ });
    $events.emit('A');
    ck $taps, 1, 'the shared block ran once';
    ck @a, ['p/A'], 'the first tap gets the value';
    ck @b, ['p/A'], 'the second tap gets it too';
}

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
