# Regression: the interpolating word subscript `%h«…»` / `%h<<…>>`.
#
# It did not parse as a postcircumfix at all. `say %h«a»` read `%h` and then a
# word list ("Useless use of constant string"), `say(%h«a»)` was "expected )",
# `%h<<a>>` — which the lexer fuses into one hyper-operator token — was
# "Missing required term after infix", and `%h«a»==1` printed the whole hash.
# It is `%h{«…»}`: the words interpolate, an interpolated value splits on
# whitespace, and one word is a single key rather than a one-key slice.
#
# The `«…»` list itself was fixed alongside, since the subscript is built from
# it: `«a»` was a one-element List instead of "a", `«$y»` did not split "a b",
# `«a$x»` glued into one word, `«"$y"»` lost its quotes and split, `«"4.5"»`
# was not val()d, and `«{"a"}»` called a routine `a`. `"%h«a»"` did not
# interpolate. `qqww` shares the word rules, and `qqww{$y}` did not split.
#
# Every expectation was checked against Rakudo 2026.08. (The radix number in
# guillemets, `:10«42»`, which the new subscript must not take, is roast's
# S32-str/val.t; Rakudo 2026.08 rejects that spelling, so it is not here.)

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my %h = a => 1, b => 2;
my $h = { a => 1 };
my $x = "a";
my $y = "a b";
my $e = "";
my @k = <a b>;
my %n = x => { y => 5 };

# the subscript
ck(%h«a», 1, '%h«a»');
ck((%h«a»), 1, 'inside parens');
ck(%h<<a>>, 1, '%h<<a>> (one fused token)');
ck(%h«a b», (1, 2), 'two words slice');
ck(%h<<a b>>, (1, 2), 'two words, ASCII');
ck(%h«$x», 1, 'an interpolated key');
ck(%h<<$x b>>, (1, 2), 'interpolated and literal');
ck(%h«$y», (1, 2), 'an interpolated value splits on whitespace');
ck(%h«"$y"», Any, 'a quoted interpolation stays one key');
ck(%h«$e», (), 'an empty interpolation is no key');
ck(%h«@k[]», (1, 2), 'an array interpolates its elements');
ck(%h«{"a"}», 1, 'a code block');
ck(%h«'a'», 1, 'a single-quoted word');
ck($h«a», 1, 'on a Hash in a scalar');
ck(%h.«a», 1, 'the dotted form');
ck($h.<<a>>, 1, 'the dotted ASCII form');
ck(%h«», %h, 'the zen slice');
ck(%n«x»«y», 5, 'chained');
ck(%n<<x>><y>, 5, 'chained with an angle subscript');
ck(%h«a»:exists, True, ':exists');
ck(%h«z»:exists, False, ':exists, absent');
ck(%h«a b»:p, (a => 1, b => 2), ':p');

# operators glued to the closer
ck(%h«a»==1, True, '«a»==');
ck(%h<<a>>==1, True, '<<a>>==');
ck(%h<<a b>>==2, True, '<<a b>>== (the >>= split re-lexed)');
ck(%h«a»+1, 2, '«a»+');
ck(%h«a»=>0, (1 => 0), '«a»=>');
ck(%h«a»===1, True, '«a»===');
ck(%h<<a>>=:=%h<<a>>, True, '<<a>>=:=');

# assignment through it
my %w = a => 1, b => 2;
%w«a» = 7;
ck(%w<a>, 7, '«a» =');
%w<<a>>=9;
ck(%w<a>, 9, '<<a>>= glued');
%w<<a b>>=3,4;
ck(%w, {a => 3, b => 4}, '<<a b>>= a slice');
%w«a» += 10;
ck(%w<a>, 13, '«a» +=');
%w«$x»:delete;
ck(%w, {b => 4}, ':delete');

# the word list the subscript is built from
ck(«a», "a", 'one word is the word');
ck(«42», IntStr.new(42, "42"), 'one numeric word is an allomorph');
ck(«$x», "a", 'one interpolation of one word');
ck(«$y», ("a", "b"), 'one interpolation of two words');
ck(«$e», (), 'one interpolation of none');
ck(«x$e», ("x",), 'more than one part never collapses');
ck(«a$x», ("a", "a"), 'a literal run and an interpolation are two words');
ck(«1$x», (IntStr.new(1, "1"), "a"), '…and the literal run is val()d');
ck(«@k[]», ("a", "b"), 'an array');
ck(«"a b" c», ("a b", "c"), 'a quoted span is one word');
ck(«x"a b"», ("x", "a b"), 'a glued quoted span is its own word');
ck(«a "b c" $y», ("a", "b c", "a", "b"), 'all together');
ck(«\$x», '$x', 'an escaped sigil');
ck(«:a($x)», (a => "a"), 'a colonpair');
ck(<<ab>>, "ab", 'the fused ASCII form in term position');
ck(«», (), 'empty');
ck(«"4.5"», RatStr.new(4.5, "4.5"), 'a quoted word is val()d');
my $num = "4.5";
ck(«"$num"», RatStr.new(4.5, "4.5"), '…and a quoted interpolation, whole');
ck(«"$y"», "a b", '…which does not split');

# qqww shares the word rules; only :v val()s, and only :v collapses
ck(qqww{$y}, ("a", "b"), 'qqww splits an interpolated value');
ck(qqww{$x}, ("a",), 'qqww keeps a one-word interpolation a List');
ck(qqww{a$x}, ("a", "a"), 'qqww splits a word at its interpolation');
ck(qqww{$num}, ("4.5",), 'qqww does not val()');
ck(qqww:v{$num}, RatStr.new(4.5, "4.5"), 'qqww:v does');
ck(qqww:v[1 2/3 $num 6e7 8+9i ten], «1 2/3 $num 6e7 8+9i ten», 'qqww:v[…] is «…»');

# in a string
ck("%h«a»", "1", '"%h«a»"');
ck("%h<<a>>", "1", '"%h<<a>>"');
ck("$h«a»!", "1!", '"$h«a»" followed by text');
ck("%h«a b»", "1 2", 'a slice in a string');
ck("a « b » c", "a « b » c", 'a guillemet that follows no variable is text');

# and a spaced hyper is still a hyper
my @p = 1, 2;
my @q = 3, 4;
ck(@p «+» @q, [4, 6], '@p «+» @q');
ck(@p <<+>> @q, [4, 6], '@p <<+>> @q');
ck(@p »+« @q, [4, 6], '@p »+« @q');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
