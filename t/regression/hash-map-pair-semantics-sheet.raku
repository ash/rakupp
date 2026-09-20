# Regression: the Hash/Map/Pair semantics sheet
# (docs/dev/findings/semantics/Hash-Map-Pair.md), implemented 2026-09-20.
#
# Every expectation below is the Rakudo 2026.08 answer, taken by running the
# sheet's own probes through the Homebrew binary rather than read off the page.
# Two of the sheet's recorded outputs are NOT used: HM-15's `.Bag`/`.Mix` probe
# prints in hash order, which the two engines do not share and Rakudo does not
# repeat between runs, and HM-01's `.last` renders itemized in the MESSAGE but
# plain through the attribute. This file passes under Rakudo too, so it is a
# parity test rather than a snapshot of us.
#
# The sheet items each block covers are named in its heading.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }
sub is-raku($got, $want, $what) { ok($got eq $want, "$what (got $got, want $want)") }

# --- HM-01: an odd store is an error, and it says what it saw ------------------
{
    my $e = (try { my %h = 1, 2, 3 }) // $!;
    is-raku($e.^name,     'X::Hash::Store::OddNumber', 'an odd count throws');
    is-raku($e.found.raku, '3',                        '…carrying the count');
    is-raku($e.last.raku,  '3',                        '…and the leftover');
}
{
    my $e = (try { my %h = ([a => 1],) }) // $!;
    is-raku($e.last.raku, '[:a(1)]', 'a lone Array element is the leftover');
}
is-raku(((try { my %h = 1 }) // $!).^name, 'X::Hash::Store::OddNumber', 'one item is odd too');
is-raku(((try %(1, 2, 3)) // $!).^name,    'X::Hash::Store::OddNumber', '%( ) stores, so it throws');
is-raku(((try Hash.new(1, 2, 3)) // $!).^name, 'X::Hash::Store::OddNumber', 'Hash.new too');
is-raku(((try (1, 2, 3).Hash) // $!).^name,    'X::Hash::Store::OddNumber', '…and .Hash');
ok(((try { my %h = { $_ } }) // $!).message.starts-with(
       'Cannot use a Callable as the only argument to store in a Hash'),
   'a lone Callable is its own error');

# --- HM-03/HM-12: an undefined key and an undefined topic both warn ------------
{
    my @w;
    my %h;
    { CONTROL { when CX::Warn { @w.push(1); .resume } }; %h{Any} = 1 }
    is-raku(%h.keys.raku, '("",).Seq', 'an undefined key stringifies to ""');
    is-raku(@w.elems.raku, '1',        '…and warns on the way');
}
{
    my @w;
    my %h = a => 1;
    my $r;
    { CONTROL { when CX::Warn { @w.push(1); .resume } }; $r = (Any ~~ %h) }
    is-raku($r.raku,       'Bool::False', 'an undefined topic finds no key');
    is-raku(@w.elems.raku, '1',           '…and warns');
}

# --- HM-04: object hashes key by identity, and check their key type -----------
{
    my %h{Int} = 1 => "a", 2 => "b";
    is-raku(%h.keys.sort.raku,    '(1, 2).Seq', 'the keys stay Int');
    is-raku((%h{1}:exists).raku,  'Bool::True',  'an Int key is found');
    is-raku((%h<1>:exists).raku,  'Bool::False', '…and a Str one is not');
    is-raku(((try { %h<x> = 1 }) // $!).^name,
            'X::TypeCheck::Binding::Parameter', 'a wrong key type is refused');
    is-raku(((try { %h{1.5} = 1 }) // $!).^name,
            'X::TypeCheck::Binding::Parameter', '…a Rat one too');
}
{
    my %h{Any};
    %h{1} = "a"; %h{"1"} = "b";
    is-raku(%h.elems.raku, '2', '1 and "1" are two keys');
    is-raku(%h.keys.map(*.^name).sort.raku, '("Int", "Str").Seq', '…of their own types');
    is-raku(%h{1}.raku, '"a"', 'the Int key reads its own value');
    is-raku(%h{"1"}.raku, '"b"', '…and the Str key its own');
}
{
    my %h{Any} = "a" => 42, "b" => 0;
    is-raku(%h.pairs.sort.raku, '(:a(42), :b(0)).Seq', 'a Str key still prints as one');
    is-raku(%h.gist, '{a => 42, b => 0}',              '…in the gist');
    is-raku(%h.Str.raku, '"a\t42\nb\t0"',              '…and in .Str');
}
is-raku(:{ 1 => "a" }.^name, 'Hash[Mu,Mu,Any]', 'the :{ } composer names three parameters');
is-raku(:{ }.keyof.raku,     'Mu',              '…but keys on Mu');
is-raku(:{ 1 => "a" }.raku,  '(my Mu %{Mu} = 1 => "a")', '…and rebuilds as a declaration');
{
    my %h{Any};
    %h{[1, 2]} = "a";
    is-raku(%h{[1, 2]}.raku, '("a", Any)', 'an unitemized aggregate key is a slice');
    is-raku(%h.elems.raku,   '2',          '…so it made two entries');
}

# --- HM-05/HM-06: a typed hash prints its declaration --------------------------
is-raku((my Int %t1).raku,               '(my Int %)',          'an empty typed hash names its type');
is-raku((my Int %t2 = a => 1).raku,      '(my Int % = :a(1))',  '…and a filled one');
is-raku((my Int %t3 is default(9)).raku, '(my Int %)',          '…`is default` does not change that');
is-raku((my %t4 is default(0) = a => 1).raku, '{:a(1)}',        '…and alone it prints plainly');
{
    my Int %h = a => 1;
    %h<a> = Nil;
    is-raku(%h.raku, '(my Int % = :a(Int))', 'Nil stores the element type object');
}

# --- HM-07: the adverb combinations that do not go ------------------------------
{
    my %h = a => 1, b => 2;
    my $f = %h<a b>:exists:k;
    is-raku($f.^name, 'Failure', ':exists:k is a Failure, not an answer');
    is-raku($f.exception.^name, 'X::Adverb',            '…carrying X::Adverb');
    is-raku($f.exception.nogo.raku, '("exists", "k").Seq', '…naming the pair that clashed');
    is-raku((%h<a b>:k:v).exception.nogo.raku, '("k", "v").Seq', 'two presentations clash too');
    is-raku((%h<a b>:exists:kv).raku, '("a", Bool::True, "b", Bool::True)', ':exists:kv is legal');
    is-raku((%h<a b>:exists:p).raku,  '(:a, :b)',                           '…and :exists:p');
    is-raku(((try %h<a>:foo) // $!).^name, 'X::Adverb', 'an UNKNOWN adverb still throws');
}

# --- HM-11: the gist is capped, .Str and .raku are not -------------------------
{
    my %h = (1..150).map({ $_ => 1 });
    is-raku(%h.gist.chars.raku,    '951',      'the gist stops at 100 pairs');
    is-raku(%h.gist.substr(*-6),   ', ...}',   '…and says so');
    ok(%h.Str.chars == 791 && %h.raku.chars == 1692, '.Str and .raku stay complete');
}

# --- HM-12: an associative topic is compared, not looked up --------------------
{
    my %h = a => 1, b => 2;
    my $m = Map.new((a => 1, b => 2));
    is-raku((%h ~~ %(a => 1, b => 2)).raku, 'Bool::True',  'a Hash topic is eqv');
    is-raku((%h ~~ %(a => 1)).raku,         'Bool::False', '…entry for entry');
    is-raku((%h ~~ $m).raku,                'Bool::False', '…and the type counts');
    is-raku(($m ~~ $m).raku,                'Bool::True',  'a Map matches a Map');
    is-raku(("a" ~~ %h).raku,               'Bool::True',  'a Str topic is still a key test');
    is-raku((<a z> ~~ %h).raku,             'Bool::True',  '…and a list any-key test');
}

# --- HM-13/HM-14: a Map is immutable, and a missing key is Nil -----------------
is-raku(Map.new.raku,  'Map.new',     'the empty Map has no argument list');
is-raku(Map.new.gist,  'Map.new(())', '…but its gist keeps one');
is-raku($(Map.new((a => 1))).raku, '$(Map.new((:a(1))))', 'an itemized Map carries its $');
{
    my $m = Map.new((a => 1, b => 2));
    is-raku(((try { $m<a> = 2 }) // $!).message, "Cannot change key 'a' in an immutable Map",
            'changing a key it has');
    is-raku(((try { $m<c> = 2 }) // $!).message, "Cannot add key 'c' to an immutable Map",
            '…and adding one it has not');
    is-raku(((try { $m<a>:delete }) // $!).^name, 'X::AdHoc',           ':delete dies');
    is-raku(((try { $m<a> := 2 }) // $!).^name,   'X::Bind',            'binding dies');
    is-raku(((try { $m.default }) // $!).^name,   'X::Method::NotFound', 'there is no .default');
    is-raku($m<z>.raku,        'Nil',       'a missing key is Nil');
    is-raku($m<a z>.raku,      '(1, Nil)',  '…in a slice too');
    is-raku(($m<z>:exists).raku, 'Bool::False', '…and it was not created');
}
{
    my %h := Map.new((a => 1));
    is-raku(((try { %h = (b => 1) }) // $!).^name, 'X::Assignment::RO', 'a bound Map refuses a store');
}
{
    my @w;
    my $r;
    { CONTROL { when CX::Warn { @w.push(1); .resume } }; $r = Map.new((a => 1)).contains("a") }
    is-raku($r.raku,       'Bool::True', '.contains reads the Str form');
    is-raku(@w.elems.raku, '1',          '…and warns that it did');
}
{
    my %h = a => 1;
    my $m = %h.Map;
    is-raku($m.^name, 'Map', 'a Hash coerces to a Map');
    my %g = $m;
    is-raku(%g.^name, 'Hash', '…and a Map stores back into a Hash');
    %g<b> = 2;
    is-raku(%g.raku, '{:a(1), :b(2)}', '…which is then writable');
}

# --- HM-15: coercions and formatting -------------------------------------------
is-raku(%(a => 0, b => 1).Set.raku,  'Set.new("b")',   'Set keeps truthy values');
is-raku(%(a => 0, b => 1).Bag.raku,  '("b"=>1).Bag',   'Bag keeps positive weights');
is-raku(%(a => -1, b => 2).Bag.raku, '("b"=>2).Bag',   '…and drops negative ones');
ok(%(a => -1, b => 2).Mix.total == 1,                  'Mix keeps a negative weight');
{
    my %h = a => 1, b => 2;
    is-raku(%h.fmt.split("\n").sort.join("|"),          "a\t1|b\t2", '.fmt is key-tab-value per line');
    is-raku(%h.fmt("%s=%s", ",").split(",").sort.join("|"), 'a=1|b=2', '…with a format and separator');
    is-raku(%h.fmt("%s").split("\n").sort.join("|"),    'a|b',       'one directive takes the key alone');
}

# --- HM-16/HM-17: what a Pair prints, and the two spellings of one --------------
is-raku((a => 1).raku,        ':a(1)',       'an identifier key is a colon pair');
is-raku(("a-b" => 1).raku,    ':a-b(1)',     '…a hyphen between words counts');
is-raku(("a\'b" => 1).raku,   ':a\'b(1)',    '…and an apostrophe');
is-raku(("é" => 1).raku,      ':é(1)',       '…and a Unicode letter');
is-raku(("a-" => 1).raku,     '"a-" => 1',   'a TRAILING hyphen does not');
is-raku(("-a" => 1).raku,     '"-a" => 1',   '…nor a leading one');
is-raku(("a-1" => 1).raku,    '"a-1" => 1',  '…nor a digit after one');
is-raku((a => True).raku,     ':a',          'a True value is the flag form');
is-raku((a => False).raku,    ':!a',         '…and False the negated one');
is-raku((a => Bool).raku,     ':a(Bool)',    '…but the type object is not');
is-raku(("a b" => True).raku, '"a b" => Bool::True', '…and a non-identifier key spells it out');
is-raku({a => True}.raku,     '{:a(Bool::True)}',    'a HASH spells it out too');
is-raku((a => 1).raku(:arglist), '"a" => 1', ':arglist forces the arrow form');
is-raku((a => 1).antipair.raku, '1 => "a"',  '.antipair keeps the value\'s type as the key');
is-raku(pair("a", 1).raku,    ':a(1)',       'the pair sub builds one');
is-raku(pair(1, 2).raku,      '1 => 2',      '…keeping a non-Str key');
is-raku(("a" ⇒ 1).raku,       ':a(1)',       '⇒ is the fat arrow');

# --- HM-18: a Pair BINDS its value ---------------------------------------------
{
    my $x = 1;
    is-raku(((try { my $p = (a => 1); $p.value = 5; "ok" }) // $!).^name,
            'X::Assignment::RO', 'a literal value is read-only');
    is-raku(((try { my $p = (a => $x + 1); $p.value = 5; "ok" }) // $!).^name,
            'X::Assignment::RO', '…an expression result too');
    is-raku(((try { my $p = :a(1); $p.value = 5; "ok" }) // $!).^name,
            'X::Assignment::RO', '…and the colon spelling');
    is-raku(((try { my $p = Pair.new("a", 1); $p.value = 5; "ok" }) // $!).^name,
            'X::Assignment::RO', '…and Pair.new');
    is-raku(((try { my $p = (a => 1).clone; $p.value = 5; "ok" }) // $!).^name,
            'X::Assignment::RO', '…and a clone of one');
    is-raku(((try { my @b = (a => 1); my $p = @b[0]; $p.value = 5; "ok" }) // $!).^name,
            'X::Assignment::RO', '…and one stored in an array');
    is-raku(((try { my $p = (a => $x); $p.value = 5; "ok" }) // "X").^name ~ "", 'Str',
            'a VARIABLE value is writable');
    is-raku(((try { my $p = :a($x); $p.value = 5; "ok" }) // "X"), 'ok',
            '…through the colon spelling too');
    is-raku(((try { my $p = (a => 1); $p.key = "b"; "ok" }) // $!).^name,
            'X::Assignment::RO', 'the KEY is never writable');
}

# --- HM-19: a Pair is a value only while its parts are -------------------------
is-raku((a => 1).WHICH.^name,   'ValueObjAt', 'a value-valued Pair identifies by content');
is-raku((a => [1]).WHICH.^name, 'ObjAt',      '…and an Array-valued one by identity');
is-raku(((a => 1) === (a => 1)).raku,     'Bool::True',  'so two equal literals are identical');
is-raku(((a => [1]) === (a => [1])).raku, 'Bool::False', '…and two equal Arrays are not');
is-raku(((a => 1), (a => 1)).unique.elems.raku,     '1', '.unique follows ===');
is-raku(((a => [1]), (a => [1])).unique.elems.raku, '2', '…both ways');

# --- HM-20: Pair.ACCEPTS, in its three shapes ----------------------------------
is-raku(((a => 1) ~~ (a => 1)).raku,   'Bool::True',  'a Pair topic matches key and value');
is-raku(((a => 1) ~~ (a => Int)).raku, 'Bool::True',  '…the value by ACCEPTS');
is-raku(((a => Int) ~~ (a => 1)).raku, 'Bool::False', '…which is not symmetric');
is-raku((("a" => 1) ~~ (Str => 1)).raku, 'Bool::False', '…and the key by ACCEPTS as well');
is-raku((%(a => 1, b => 2) ~~ (a => 1)).raku, 'Bool::True', 'an Associative topic looks the key up');
is-raku((%(a => 1) ~~ (a => 2)).raku,  'Bool::False', '…and matches what it finds');
is-raku(("abc" ~~ (chars => 3)).raku,  'Bool::True',  'anything else calls the key as a method');
is-raku(((1, 2) ~~ (elems => 2)).raku, 'Bool::True',  '…a List included');
is-raku((42 ~~ (is-prime => False)).raku, 'Bool::True', '…comparing the Bools');
is-raku(((try 42 ~~ (:frobnicate)) // $!).^name, 'X::Method::NotFound',
        'a key that names no method is an error');
is-raku(((a => 1) ~~ %(a => 1)).raku, 'Bool::False', 'a Pair TOPIC is looked up by its Str');

say $fails ?? "FAIL ($fails)" !! "PASS";
