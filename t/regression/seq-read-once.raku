# Regression: a Seq is read once unless it is cached (ROAST-TRACKS-PLAN
# track B, phase B3, 2026-09-27). A second iteration is X::Seq::Consumed;
# everything that KEEPS a Seq's values (.elems, .Bool, .Str, `$s[0]`, `@$s`,
# an operator, a built-in sub, an `@` parameter) caches it, after which it can
# be read any number of times. Before: a Seq could be read without end, and
# `.sink` of a gather ran none of its block.
#
# The half that matters most is the second one: every use Rakudo caches must
# cache here too, or a program that works there dies here.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub twice($s) { (try { for $s<> { }; for $s<> { }; 'fine' }) // $!.^name }

{
    my $s = (1, 2, 3).map(* + 1);
    for $s<> { }
    ck((try { for $s<> { }; 'fine' }) // $!.^name, 'X::Seq::Consumed', 'a Seq read twice');
    ck((try $s.elems) // $!.^name, 'X::Seq::Consumed', '…and asked for its size afterwards');
    ck($s.raku, '$(Seq.new())', 'a consumed Seq prints as Seq.new()');
    ck((try Seq.new.iterator) // $!.^name, 'X::Seq::Consumed', '…which is a Seq read already');
    my \n = (1, 2).grep({ $_ });
    ck((try { my $x = 0; $x++ for n; $x++ for n; $x }) // $!.^name, 'X::Seq::Consumed',
       'a sigilless Seq in two `for` modifiers');
}

{
    my %uses =
        'elems'       => -> $s { $s.elems },       'Bool'        => -> $s { $s.Bool },
        'Str'         => -> $s { $s.Str },         'gist'        => -> $s { $s.gist },
        'cache'       => -> $s { $s.cache },       'subscript'   => -> $s { $s[0] },
        '@$s'         => -> $s { @$s },            'if'          => -> $s { if $s { } },
        'prefix +'    => -> $s { +$s },            'interpolate' => -> $s { "$s" },
        '=='          => -> $s { $s == 3 },        'cmp'         => -> $s { $s cmp (1, 2) },
        '~~'          => -> $s { $s ~~ (2, 3, 4) }, 'elems()'    => -> $s { elems($s) },
        '@ parameter' => -> $s { sub g(@a) { }; g($s) },
        '*@ slurpy'   => -> $s { sub f(*@a) { }; f($s<>) };
    for %uses.sort(*.key) -> (:key($what), :value(&apply)) {
        my $s = (1, 2, 3).map(* + 1);
        apply($s);
        ck(twice($s), 'fine', "$what caches a Seq");
    }
    my $t = (1, 2, 3).map(* + 1);
    sub count($x) { $x.elems }
    count($t);
    ck(twice($t), 'fine', '…also when it is cached inside a routine it was passed to');
}

{
    my $s := (1..3).Seq;
    my $skipped := $s.skip;
    ck((try @$s) // $!.^name, 'X::Seq::Consumed', '.skip reads the Seq it skips');
    ck(@$skipped.List, (2, 3), '…and answers a Seq of its own');
    my $sliced = (1, 2, 3).Seq.slice(0, 1, 2);
    ck($sliced.iterator.pull-one, 1, 'an iterator taken');
    ck((try $sliced[0]) // $!.^name, 'X::Seq::Consumed', '…leaves nothing to index');
}

{
    my $ran = 0;
    my $g = gather { $ran++; take 1 };
    $g.sink;
    ck($ran, 1, '.sink of a gather runs its block');
    my $cached-ran = 0;
    my $h = gather { $cached-ran++; take 1 };
    $h.cache;
    $h.sink;
    ck($cached-ran, 0, '…and of a cached one runs nothing');
    my $list-ran = 0;
    (gather { $list-ran++; take 1 }).cache;
    ck($list-ran, 0, 'a sunk List view of a gather runs nothing');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
