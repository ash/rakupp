# Regression: the regex family takes a Unicode bracket for its delimiter, and a
# Unicode quote nests wherever it appears. Before this:
#   - `rx｢\d+｣`, `m⟨x⟩`, `s｢a｣ = "b"`, `tr｢a｣｢b｣` were calls to undeclared
#     routines, or parse errors. Lexer::tryQuoteForm gave the regex family a
#     Unicode SYMBOL delimiter (`m°b°`) but no Unicode bracket, since its
#     bracket scan held the delimiters as single chars. The scan now holds
#     them as strings, and a Unicode bracket is quote-aware like a lone
#     delimiter, so `rx｢ a '｣' b ｣` and `m｢ <[x｣]> ｣` end where Rakudo says;
#   - `/ ｢a ｢b｣ c｣ /` matched only `a ｢b`. Regex.cpp, Lexer::skipUniQuote and
#     the code-block scanners (uniQuoteSpanEnd) stopped at the first closer
#     of a ‘…’, “…”, ｢…｣, whereas Rakudo's quotes nest on their own opener;
#     the bare low-9 strings `‚a ‚b’ c’` / `„a „b” c”` did not nest either;
#   - an ASCII BRACKET delimiter was not quote-aware at all: the first `}`
#     ended `m{ '}' }` inside its quote.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box is rakupp. Green on both engines.

use MONKEY-SEE-NO-EVAL;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- a Unicode quote inside a regex nests -----------------------------------
ck so("a ｢b｣ c" ~~ / ｢a ｢b｣ c｣ /),       True,        '/ ｢…｣ / nests';
ck so("a ｢b｣ c" ~~ m/ ｢a ｢b｣ c｣ /),      True,        'm/ ｢…｣ / nests';
ck so("a ｢b｣ c" ~~ rx/ ｢a ｢b｣ c｣ /),     True,        'rx/ ｢…｣ / nests';
ck ("a ‘b’ c" ~~ / ‘a ‘b’ c’ /).Str,      "a ‘b’ c",   '/ ‘…’ / nests';
ck ("a “b” c" ~~ / “a “b” c” /).Str,      "a “b” c",   '/ “…” / nests';
ck ("a ‚b’ c" ~~ / ‚a ‚b’ c’ /).Str,      "a ‚b’ c",   '/ ‚…’ / nests';
ck ("a/b" ~~ / ｢a/b｣ /).Str,              "a/b",       'a delimiter inside ｢…｣ is text';

# --- …and so does a bare low-9 string, and one inside a code block -------------
ck ‚a ‚b’ c’,               "a ‚b’ c",     '‚…’ nests';
ck „a „b” c”,               "a „b” c",     '„…” nests';
ck "x {｢a ｢}｣ c｣} y",       "x a ｢}｣ c y", 'a ｢…｣ in a closure nests past its }';
ck "x {‘a ‘}’ c’} y",       "x a ‘}’ c y", 'a ‘…’ in a closure nests past its }';

# --- rx / m with a Unicode bracket ----------------------------------------------
for 'rx｢x｣', 'm｢x｣', 'rx⟨x⟩', 'm「x」', 'rx『x』', 'rx【x】', 'rx（x）', 'rx⦍x⦐',
    'rx«x»', 'm‘x’', 'm“x”', 'm„x”', 'rx〝x〞', 'm⦃x⦄', 'rx｢｢x｣｣',
    'rx ｢x｣', 'm ｢x｣' -> $src {
    ck so("x" ~~ EVAL $src), True, "$src matches";
}
ck so("X" ~~ m:i｢x｣),           True,  'm:i｢…｣';
ck so("X" ~~ m:i ｢x｣),          True,  'm:i ｢…｣, after a space';
ck so("a b" ~~ rx:s｢a b｣),      True,  'rx:s｢…｣';
ck so("a b" ~~ ms｢a b｣),        True,  'ms｢…｣';
ck ("abc" ~~ m:g｢\w｣)».Str,     ("a", "b", "c"), 'm:g｢…｣';
ck ("x42".match(rx「\d+」)).Str, "42", 'rx「…」 as an argument';
{
    my $r = rx｢\d+｣;
    ck ("x42" ~~ $r).Str, "42", 'rx｢…｣ stored and used';
}

# --- the pattern between Unicode brackets is a regex ------------------------------
ck ("abc" ~~ rx｢a ｢b｣ c｣).Str,      "abc",  'a nested ｢b｣ is a quoted literal';
ck ("ab" ~~ rx｢ a [ b ] ｣).Str,      "ab",   'a group';
ck ("a]b" ~~ rx｢ a ']' b ｣).Str,     "a]b",  "a quoted ]";
ck ("a｣b" ~~ rx｢ a '｣' b ｣).Str,     "a｣b",  'a quoted closer is text';
ck ("a｣b" ~~ rx｢ a \｣ b ｣).Str,      "a｣b",  'an escaped closer is text';
ck ("x" ~~ m｢ <[x｣]> ｣).Str,         "x",    'a closer in a character class is a member…';
ck ("｣" ~~ m｢ <[x｣]> ｣).Str,         "｣",    '…and matches';
ck ("xy" ~~ m｢ x { "｣" } y ｣).Str,   "xy",   'a closer in a code block is code';

# --- s: only the assignment form ------------------------------------------------------
{
    $_ = "aaa"; s｢a｣ = "c";        ck $_, "caa", 's｢…｣ = …';
    $_ = "aaa"; s:g｢a｣ = "c";      ck $_, "ccc", 's:g｢…｣ = …';
    $_ = "aaa"; s ｢a｣ = "c";       ck $_, "caa", 's ｢…｣ = …, after a space';
    $_ = "aaa"; s⟨a⟩ = "c";        ck $_, "caa", 's⟨…⟩ = …';
    $_ = "a b"; ss｢a b｣ = "c d";   ck $_, "c d", 'ss｢…｣ = …';
    $_ = "aaa"; my $r = S｢a｣ = "c";
    ck ($r, $_), ("caa", "aaa"), 'S｢…｣ = … answers the new string';
}
for 's｢a｣｢b｣', 's｢a｣ ｢b｣', 's:g｢a｣｢b｣', 's⟨a⟩⟨b⟩', 'S｢a｣｢b｣' -> $src {
    try EVAL '$_ = "aaa"; ' ~ $src;
    ck $!.^name, 'X::Syntax::Missing', "$src: Missing assignment operator";
}

# --- tr / TR take two groups ----------------------------------------------------------
{
    $_ = "abc"; tr｢a..c｣｢A..C｣;   ck $_, "ABC", 'tr｢…｣｢…｣';
    $_ = "abc"; tr⟨a⟩⟨x⟩;         ck $_, "xbc", 'tr⟨…⟩⟨…⟩';
    $_ = "abc"; ck TR｢a｣｢x｣, "xbc", 'TR｢…｣｢…｣';
}

# --- an ASCII bracket delimiter is quote-aware too ---------------------------------------
ck ("x}y" ~~ m{ 'x}y' }).Str,       "x}y",  "m{ '}' }";
ck ("x}y" ~~ m{ ｢x}y｣ }).Str,       "x}y",  'm{ ｢}｣ }';
ck ("x]y" ~~ rx[ x "]" y ]).Str,    "x]y",  'rx[ "]" ]';
ck ("a>b" ~~ rx< a ">" b >).Str,    "a>b",  'rx< ">" >';
ck ("x" ~~ m{ <[x}]> }).Str,        "x",    'm{ <[}]> }';
ck ("ab" ~~ rx{ a # a comment's }
     b }).Str,                      "ab",   'a comment ends at the line, not at a }';

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
