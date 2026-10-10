# Regression: a quote WORD with a Unicode bracket for its delimiter nests the
# pair, as every bracketing quote in Raku does. `Q｢outer ｢x｣ y｣` died
# "unexpected operator in term position (got '｣')": the first ｣ ended the
# string and left ` y｣` behind as code. A bare ｢…｣ (and 「…」, 『…』) already
# nested in the main tokenizer; the quote-word path, Lexer::tryQuoteForm's
# arbitrary-Unicode-delimiter branch, did not, for any pair it accepts —
# ｢｣ ⟨⟩ 【】 （） ‘’ “” and the rest. The same branch now also takes a
# repeated opener as one delimiter (`Q｢｢a｣b｣｣`), gives `q` its escapes
# (`\\` and a backslashed delimiter), and keeps a bare `Q` escape-free.
# Guillemets went through a branch of their own that nested only single «»
# and knew no escapes; they share this one now.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box is rakupp. Green on both engines.

use MONKEY-SEE-NO-EVAL;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the report: every quote word over ｢…｣ ----------------------------------
ck Q｢outer ｢x｣ y｣,   'outer ｢x｣ y', 'Q｢…｣ nests';
ck q｢a ｢b｣ c｣,       'a ｢b｣ c',     'q｢…｣ nests';
ck qq｢a ｢b｣ c｣,      'a ｢b｣ c',     'qq｢…｣ nests';
ck Q:b｢a\t｢b｣｣,      "a\t｢b｣",      'Q:b｢…｣ nests';
ck q:s｢a ｢b｣ c｣,     'a ｢b｣ c',     'q:s｢…｣ nests';
ck Qs｢a ｢b｣ c｣,      'a ｢b｣ c',     'Qs｢…｣ nests';
ck q｢a ｢b ｢c｣ d｣ e｣, 'a ｢b ｢c｣ d｣ e', 'nesting goes deeper than one level';
ck Q｢a｢｣b｣,          'a｢｣b',        'an empty nested pair';
{
    my $x = 5;
    ck qq｢x $x ｢{1+1}｣｣, 'x 5 ｢2｣', 'qq interpolates inside the nested pair';
}

# --- the other bracket pairs tryQuoteForm accepts ------------------------------
for <｢ ｣  「 」  『 』  ⟨ ⟩  ⟦ ⟧  « »  【 】  〈 〉  《 》  〔 〕  〖 〗  〘 〙
     ⁅ ⁆  ⦃ ⦄  ⦅ ⦆  ⦍ ⦐  ⸨ ⸩  ❨ ❩  ⌈ ⌉  （ ）  ［ ］  ｛ ｝  〝 〞
     ‘ ’  “ ”  ‚ ’  „ ”> -> $o, $c {
    my $want = "a {$o}b$c c";
    for <Q q qq> -> $w {
        my $got = try EVAL $w ~ $o ~ "a {$o}b$c c" ~ $c;
        ck $got, $want, "$w$o…$c nests";
    }
}

# --- a repeated opener is ONE delimiter ---------------------------------------
ck Q｢｢a｣b｣｣,           'a｣b',          'Q｢｢…｣｣: a single ｣ is text';
ck q｢｢a ｢｢b｣｣ c｣｣,     'a ｢｢b｣｣ c',    '…an equally long run nests';
ck q｢｢a ｢b｣ c｣｣,       'a ｢b｣ c',      '…and a single pair inside is text';
ck Q⟨⟨a⟩b⟩⟩,           'a⟩b',          'Q⟨⟨…⟩⟩';
ck Q‘‘a’b’’,           'a’b',          'Q‘‘…’’';
ck Q««a ««b»» c»»,     'a ««b»» c',    'Q««…»» nests an equally long run';
ck Q««a «b» c»»,       'a «b» c',      '…and keeps a single «» as text';

# --- q's escapes: \\ and a backslashed delimiter --------------------------------
ck q｢a \｣ b｣,          'a ｣ b',        'q: \｣ is a ｣';
ck q｢a \｢ b｣,          'a ｢ b',        'q: \｢ is a ｢';
ck q｢a \\ b｣,          'a \ b',        'q: \\ is one backslash';
ck q｢a \x b｣,          'a \x b',       'q: any other backslash stays';
ck q⟨a ⟨\⟩ b⟩ c⟩,       'a ⟨⟩ b⟩ c',    'q: an escaped closer does not unnest';
ck q«a \» b»,          'a » b',        'q: \» is a »';
ck q｢｢a \｣｣ b｣｣,       'a ｣｣ b',       'q: an escaped closer run';
ck q♥a\♥b♥,            'a♥b',          'q: an escaped symbol delimiter';
ck qq｢a \｣ b｣,         'a ｣ b',        'qq: \｣ is a ｣';

# --- …and a bare Q has none -----------------------------------------------------
ck Q｢a \\ b｣,          'a \\\\ b',     'Q: \\ is two backslashes';
ck Q♥a\♥,              'a\\',          'Q: a backslash does not protect the delimiter';
ck (try EVAL 'Q｢a \｣ b｣').defined, False, 'Q: \｣ ends the string';

# --- word lists and heredocs over a nesting pair --------------------------------
ck qw｢a ｢b｣ c｣,        ("a", "｢b｣", "c"),       'qw｢…｣ nests';
ck q:w⟨a ⟨b c⟩ d⟩,      ("a", "⟨b", "c⟩", "d"),  'q:w⟨…⟩ nests, then splits';
ck qw«a «b» c»,        ("a", "«b»", "c"),       'qw«…» nests';
ck q:to«END», "hi\n", 'a «…» heredoc terminator';
hi
END

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
