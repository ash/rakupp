# Regression: a `{…}` closure inside a `qq` quote is Raku code, and a string in
# it may hold the quote's own closing delimiter. `qq[a {"]"} b]` is `a ] b` in
# Rakudo; Raku++ ended the quote at the `]` inside the block's string and
# reported "Unable to parse expression in double quotes". Lexer::tryQuoteForm
# scanned a `$var.method("…")` chain as code (interpChainEnd) but not a closure,
# and its Unicode-delimiter branch (`qq｢…｣`) scanned neither. Every qq-family
# form with the closure feature (qq, qqw, qqx, q:c, Q:c, qq:s, …) now copies a
# `{…}` block as code, quotes and all, on the ASCII, repeated-delimiter and
# Unicode paths, and the Unicode path reads interpolation chains as code too
# (`qq｢@a.join("｣")｣`) — with the quote's own Unicode delimiters ending a name
# (`qq｢a $x｣`), since any byte >= 0x80 used to continue one.
#
# Which delimiter carries the quote is part of the rule, as in Rakudo, where
# the opener is tried before any escape: a LONE `{` delimiter makes every inner
# `{` a nesting step even under `:c` (`qq:c{a {1+1} b}` is `a {1+1} b`; it was
# `a 2 b`), and a doubled `{{` frees the single `{` again (`qq{{a {1+1} b}}` is
# `a 2 b`; it was `a {1+1} b`) while a nested `{{…}}` stays text.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box is rakupp. Green on both engines.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the report: the closer inside a closure's string ---------------------------
ck qq[a {"]"} b],                       'a ] b',     'qq[]: "]" in a closure';
ck qq｢a {"｣"} b｣,                       'a ｣ b',     'qq｢｣: "｣" in a closure';

# --- ASCII delimiters ------------------------------------------------------------
ck qq[a {'x]y'} b],                     'a x]y b',   "qq[]: '…' in a closure";
ck qq[a {"["} b],                       'a [ b',     'qq[]: an unbalanced opener in a closure';
ck qq[a {"x" ~ '[' } b],                'a x[ b',    '…in a longer expression';
ck qq[a { do { "]" } } b],              'a ] b',     'nested braces in the closure';
ck qq[a { my %h = x => "]"; %h<x> } b], 'a ] b',     'statements in the closure';
ck qq[{"]"}],                           ']',         'a closure first and last';
ck qq[a [b] {"]"} c],                   'a [b] ] c', 'next to a nested bracket pair';
ck qq[a {"]"} {"]"} b],                 'a ] ] b',   'two closures';
ck qq[a {"a\"]"} b],                    'a a"] b',   'an escaped quote in the closure string';
ck qq[a { "it's ]" } b],                "a it's ] b", 'an apostrophe in the closure string';
ck qq[a { ｢]｣ } b],                     'a ] b',     'a ｢…｣ string in the closure';
ck qq[a { “]” } b],                     'a ] b',     'a “…” string in the closure';
ck qq/a {"\/"} b/,                      'a / b',     'qq//: "\/"';
ck qq/a {"/"} b/,                       'a / b',     'qq//: "/"';
ck qq/a { 4/2 } b/,                     'a 2 b',     'qq//: division in the closure';
ck qq|a {"|"} b|,                       'a | b',     'qq||';
ck qq|a { 1 || 0 } b|,                  'a 1 b',     'qq||: || in the closure';
ck qq!a {"!"} b!,                       'a ! b',     'qq!!';
ck qq<a {">"} b>,                       'a > b',     'qq<>';
ck qq<a { 2 > 1 } b>,                   'a True b',  'qq<>: > in the closure';
ck qq[a { 1 # ]
} b],                                   'a 1 b',     'a comment in the closure';
ck qq[a {
    my $s = "]";
    $s x 2
} b],                                   'a ]] b',    'a multi-line closure';

# --- the other forms with the closure feature --------------------------------------
ck Q:c[a {"]"} b],                      'a ] b',     'Q:c';
ck q:c[a {"]"} b],                      'a ] b',     'q:c';
ck qc[a {"]"} b],                       'a ] b',     'qc';
ck Qc[a {"]"} b],                       'a ] b',     'Qc';
ck qq:s[a {"]"} b],                     'a ] b',     'qq:s';
ck Q:qq[a {"]"} b],                     'a ] b',     'Q:qq';
ck Q:closure[a {"]"} b],                'a ] b',     'Q:closure';
ck qqw[a {"]"} b],                      ("a", "]", "b"), 'qqw';
ck qq:!c [a {1+1} b],                   'a {1+1} b', 'qq:!c: no closure';
ck qq:to[END], "x ] y\n",                            'qq:to[…] heredoc';
x {"]"} y
END

# --- a repeated delimiter -----------------------------------------------------------
ck qq[[a {"]]"} b]],                    'a ]] b',    'qq[[…]]';
ck qq<<a {">>"} b>>,                    'a >> b',    'qq<<…>>';
ck qq[[a {"]]"} [[x]] b]],              'a ]] [[x]] b', '…next to a nested run';
{
    my @a = 1, 2;
    ck qq[[@a.join("]]")]],             '1]]2',      'qq[[…]]: an interpolation chain';
}

# --- Unicode delimiters -------------------------------------------------------------
ck qq｢a {'｣'} b｣,                       'a ｣ b',     "qq｢｣: '｣'";
ck qq｢a { "x" ~ "｣" } b｣,               'a x｣ b',    'qq｢｣: a longer expression';
ck qq｢a {"｢"} b｣,                       'a ｢ b',     'qq｢｣: the opener in a closure';
ck qq«a {"»"} b»,                       'a » b',     'qq«»';
ck qq“a {"”"} b”,                       'a ” b',     'qq“”';
ck qq⟨a {"⟩"} b⟩,                       'a ⟩ b',     'qq⟨⟩';
ck qq♥a {"♥"} b♥,                       'a ♥ b',     'qq♥♥: a symbol delimiter';
ck Q:c｢a {"｣"} b｣,                      'a ｣ b',     'Q:c｢｣';
ck q:c｢a {"｣"} b｣,                      'a ｣ b',     'q:c｢｣';
ck qq:!c｢a {1+1} b｣,                    'a {1+1} b', 'qq:!c｢｣: no closure';
ck qq｢a {
    "｣" x 2
} b｣,                                   'a ｣｣ b',   'qq｢｣: a multi-line closure';
{
    my @a = 1, 2;
    my %h = '｣' => 5;
    my $x = 'a';
    sub f($v) { "<$v>" }
    ck qq｢@a.join("｣")｣,                '1｣2',       'qq｢｣: a method chain holding ｣';
    ck qq｢%h{"｣"}｣,                     '5',         'qq｢｣: a hash subscript holding ｣';
    ck qq｢&f("｣")｣,                     '<｣>',       'qq｢｣: a call holding ｣';
    ck qq｢@a[]｣,                        '1 2',       'qq｢｣: a zen slice';
    ck qq｢$x｣,                          'a',         'qq｢｣: the closer ends the name';
    ck qq｢$x.uc｣,                       'a.uc',      '…and a bare .name, which is text';
    ck qq｢$x.uc()｣,                     'A',         '…and a call';
    ck qq♥a $x b♥,                      'a a b',     'qq♥♥: a variable';
    ck qq“a $x.uc()”,                   'a A',       'qq“”: a call';
}

# --- a brace delimiter --------------------------------------------------------------
ck qq{a {1+1} b},                       'a {1+1} b', 'qq{}: braces nest, no closure';
ck qq:c{a {1+1} b},                     'a {1+1} b', 'qq:c{}: …even under :c';
ck Q:c{a {1+1} b},                      'a {1+1} b', 'Q:c{}: …even under :c';
ck qq{{a {1+1} b}},                     'a 2 b',     'qq{{}}: a lone { is a closure';
ck qq:c{{a {1+1} b}},                   'a 2 b',     'qq:c{{}}';
ck qq{{a {{1+1}} b}},                   'a {{1+1}} b', 'qq{{}}: a nested {{…}} is text';
ck qq{{a {{1}} {2} b}},                 'a {{1}} 2 b', '…next to a closure';
ck qq{{a {"}}"} b}},                    'a }} b',    'qq{{}}: "}}" in a closure';
ck qq{{a \{1} b}},                      'a {1} b',   'qq{{}}: \{ opens no closure';
ck qq{{a \}} b}},                       'a }} b',    'qq{{}}: \}} is }}';
ck qqw{a {1+1} b},                      ("a", "\{1+1}", "b"), 'qqw{}: no closure';
ck qqw{{a {1+1} b}},                    ("a", "2", "b"), 'qqw{{}}: a closure';
{
    my @a = 1, 2;
    ck qq{{@a.join("}}")}},             '1}}2',      'qq{{}}: an interpolation chain';
}
ck (try EVAL Q｢qq{a {"}"} b}｣).defined, False,      'qq{}: the "}" ends the quote';

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
