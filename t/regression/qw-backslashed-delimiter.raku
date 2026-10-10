# Regression: a `q` word list has `q`'s escapes. `qw[a \] b]` was
# ("a", "\\]", "b") where Rakudo has ("a", "]", "b"), and `qw[a \\ b]` kept
# both backslashes. Lexer::tryQuoteForm collapsed `\\` and a backslashed
# delimiter for a `q` STRING, but handed a word list's text to the parser
# untouched, and the parser never knew the delimiters. They now ride on the
# QwList token, and the parser unescapes each unquoted word. That covers qw,
# q:w, qww, q:ww and :v, over ASCII and Unicode delimiters. Inside a qww's
# quoted span the span's own rules still hold.
#
# Two neighbours, fixed with it:
#   - a repeated delimiter escapes only as a whole run: `q[[a \]] b]]` is
#     `a ]] b` (it was a parse error), and in `q{{a \} b}}` the `\}` stays;
#   - a "…" (or “…”) span in qww/Qww/q:ww is a `qq` string, as in qqww:
#     `qww[a "$x\t" b]` interpolates and escapes, where it was read
#     single-quoted.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box is rakupp. Green on both engines.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the report: a backslashed delimiter is the delimiter ---------------------
ck qw[a \] b],   ("a", "]", "b"),  'qw[]: \] is ]';
ck qw[a \[ b],   ("a", "[", "b"),  'qw[]: \[ is [';
ck qw<a \> b>,   ("a", ">", "b"),  'qw<>: \> is >';
ck qw{a \} b},   ("a", "}", "b"),  'qw{}: \} is }';
ck qw/a \/ b/,   ("a", "/", "b"),  'qw//: \/ is /';
ck qw!a \! b!,   ("a", "!", "b"),  'qw!!: \! is !';
ck qw[a b\] c],  ("a", "b]", "c"), '…inside a word too';
ck qw｢a \｣ b｣,   ("a", "｣", "b"),  'qw｢｣: \｣ is ｣';
ck qw｢a \｢ b｣,   ("a", "｢", "b"),  'qw｢｣: \｢ is ｢';
ck qw⟨a \⟩ b⟩,   ("a", "⟩", "b"),  'qw⟨⟩: \⟩ is ⟩';
ck qw«a \» b»,   ("a", "»", "b"),  'qw«»: \» is »';

# --- \\ is one backslash; every other backslash stays ---------------------------
ck qw[a \\ b \x], ("a", "\\", "b", "\\x"), 'qw: \\ is \, \x stays';
ck qw[a b\\ c],   ("a", "b\\", "c"),       'qw: \\ ending a word';
ck qw[a \n b],    ("a", "\\n", "b"),       'qw: \n is no newline';
ck qw[a\ b],      ("a\\", "b"),            'qw: a backslash does not escape a space';
ck qw｢a \\ b｣,    ("a", "\\", "b"),        'qw｢｣: \\ is one backslash';

# --- every non-interpolating word form -----------------------------------------
ck q:w[a \] b],   ("a", "]", "b"),  'q:w';
ck q:w｢a \｣ b｣,   ("a", "｣", "b"),  'q:w｢｣';
ck q :w /a \/ b/, ("a", "/", "b"),  'q :w with a space';
ck q:w{a \{ b \} c}, ('a', '{', 'b', '}', 'c'), 'q:w{}: both braces';
ck qww[a \] b],   ("a", "]", "b"),  'qww';
ck qww[a \\ b],   ("a", "\\", "b"), 'qww: \\';
ck q:ww[a \] b],  ("a", "]", "b"),  'q:ww';
ck qw:v[1 \] 2],  (IntStr.new(1, "1"), "]", IntStr.new(2, "2")), 'qw:v';
ck q:w:v[a \] 3], ("a", "]", IntStr.new(3, "3")), 'q:w:v';

# --- Q has no escapes; the qq forms unescape as they interpolate ------------------
ck Qw[a \\ b],    ("a", "\\\\", "b"), 'Qw keeps \\';
ck Qww[a \\ b],   ("a", "\\\\", "b"), 'Qww keeps \\';
ck Qw[a \x b],    ("a", "\\x", "b"),  'Qw keeps \x';
ck qqw[a \] b],   ("a", "]", "b"),    'qqw: \]';
ck qqw[a \\ b],   ("a", "\\", "b"),   'qqw: \\';
ck qqww[a \] b],  ("a", "]", "b"),    'qqww: \]';

# --- a qww's quoted spans keep their own rules -----------------------------------
ck qww[a 'x \] y' b], ("a", "x \\] y", "b"), "qww '…': \\] is not its delimiter";
ck qww[a 'x \\ y' b], ("a", "x \\ y", "b"),  "qww '…': \\\\ is \\";
ck qww[a 'x\'y' b],   ("a", "x'y", "b"),     "qww '…': \\' is '";
ck qww[\] "a b"],     ("]", "a b"),          'qww: an escaped delimiter next to a span';
ck qww[a "b c" \"d],  ("a", "b c", "\\\"d"), 'qww: \" is text, not a span';
{
    my $x = 5;
    my @a = 1, 2;
    ck qww[a "x \] y" b],  ("a", "x ] y", "b"),  'qww "…" is a qq string: \]';
    ck qww[a "$x\t" b],    ("a", "5\t", "b"),    'qww "…" interpolates and escapes';
    ck qww[a "@a[] c" b],  ("a", "1 2 c", "b"),  '…an array, as one word';
    ck qww[a "{1+1}" b],   ("a", "2", "b"),      '…a block';
    ck Qww[a "$x" b],      ("a", "5", "b"),      'Qww "…" interpolates too';
    ck q:ww[a "$x" b],     ("a", "5", "b"),      'q:ww "…" interpolates too';
    ck qww[a “$x” b],      ("a", "5", "b"),      'qww “…” interpolates too';
    ck qw[a "$x" b],       ("a", "\"\$x\"", "b"), 'qw has no spans: "…" is text';
}

# --- a repeated delimiter escapes only as a whole run ------------------------------
ck q[[a \]] b]],     'a ]] b',  'q[[…]]: \]] is ]]';
ck q{{a \} b}},      'a \} b',  'q{{…}}: a lone \} stays';
ck q[[a \] b]],      'a \] b',  'q[[…]]: a lone \] stays';
ck q[[a \\ b]],      'a \ b',   'q[[…]]: \\ is one backslash';
ck q｢｢a \｣ b｣｣,      'a \｣ b',  'q｢｢…｣｣: a lone \｣ stays';
ck q｢｢a \｣｣ b｣｣,     'a ｣｣ b', 'q｢｢…｣｣: \｣｣ is ｣｣';
ck qw[[a \]] b]],    ("a", "]]", "b"),   'qw[[…]]: \]] is ]]';
ck qw{{a \} b}},     ("a", "\\}", "b"),  'qw{{…}}: a lone \} stays';
ck qw{{a \}} b}},    ("a", "}}", "b"),   'qw{{…}}: \}} is }}';
ck (try EVAL 'Q[[a \]] b]]').defined, False, 'Q[[…]]: \]] ends the string';

# --- heredoc word lists: \\ only, and a span's own rules -----------------------------
ck q:to:w/EOF/, ("a", "\\", "b", "\\/", "c", "\\x"), 'q:to:w';
a \\ b \/ c \x
EOF
ck q:heredoc:ww"EOF", ("a", "\\", "b", "c \\ d"), 'q:heredoc:ww: "…" sees its \\ once';
a \\ b "c \\ d"
EOF

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
