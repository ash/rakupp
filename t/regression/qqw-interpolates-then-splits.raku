# Regression: an interpolating word list builds its words the way Rakudo does.
#
# qqw (and qq:w, q:w:s, Qw:c — any word list without quote protection) is ONE
# interpolated string, split into words afterwards. Raku++ split the source
# text at its blanks first and interpolated each piece, so a value's words
# stayed together (`qqw[a $x b]` with $x "p q" was ("a", "p q", "b")), a
# closure or call with a blank in it fell apart (`qqw[a {1 + 1} b]` was
# ("a", "1", "+", "1}", "b"); `{"x y"}` was a parse error), `%h<k>` and
# `&f()` did not interpolate, and an escaped blank never split (`qqw[a\tb]`).
#
# qqww, «…» and <<…>> make one word per literal run and per interpolation, but
# their scanners cut at every blank and read every quote as a span, so code
# in a word broke the same way (`qqww[a {"x"} b]` was ("a", "x", "}", "b")).
# A closure, a call's arguments and `$( … )` are now one unit there, and a
# literal run splits once its escapes are applied (`qqww[a\ b]`).
#
# Neighbours fixed with it: a quote's feature adverbs reach its words
# (`qqw:!s`, `qww:c`, `Qw:s`, `qqw:!b` were ignored), and a word-list FORM
# with :to is a word list (`qqw:to/END/` was a plain, uninterpolated string).
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box is rakupp. Green on both engines.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $x = "p q";
my $y = "z";
my $e = "";
my $n = "42";
my @a = 1, 2;
my %h = k => "v w";
sub f { "f g" }

# --- qqw: interpolate, then split ----------------------------------------------
ck qqw[a $x b],              ("a", "p", "q", "b"),    'qqw: a value splits';
ck qqw[$x],                  ("p", "q"),              '…on its own';
ck qqw[$y],                  ("z",),                  'one interpolated word is a List';
ck qqw[a{$x}b],              ("ap", "qb"),            'glued text joins the value';
ck qqw[a$x],                 ("ap", "q"),             '…on one side';
ck qqw[a {1 + 1} b],         ("a", "2", "b"),         'a closure with blanks';
ck qqw[a {"x y"} b],         ("a", "x", "y", "b"),    'a closure with a quoted blank';
ck qqw[a @a[] b],            ("a", "1", "2", "b"),    'an array';
ck qqw[a @a.join(" ") b],    ("a", "1", "2", "b"),    'a call with a blank';
ck qqw[a $x.substr(0, 1) b], ("a", "p", "b"),         'a call with arguments';
ck qqw[a $(1 + 1) b],        ("a", "2", "b"),         '$( … )';
ck qqw[a %h<k> b],           ("a", "v", "w", "b"),    'a hash element';
ck qqw[a &f() b],            ("a", "f", "g", "b"),    'a call';
ck qqw[a $e b],              ("a", "b"),              'an empty value is no word';
ck qqw[$e],                  (),                      '…and alone, no words';
ck qqw[a "x y" b],           ("a", "\"x", "y\"", "b"), 'no quote protection';
ck qqw[a\tb],                ("a", "b"),              'an escaped tab splits';
ck qqw[a\ b],                ("a", "b"),              'an escaped blank splits';
ck qqw[a\nb c],              ("a", "b", "c"),         'an escaped newline splits';
ck qqw[a \] b],              ("a", "]", "b"),         'an escaped delimiter';
ck qqw[a
  $x
  b],                        ("a", "p", "q", "b"),    'across lines';
ck qqw:v[1 $y 2],            (IntStr.new(1, "1"), "z", IntStr.new(2, "2")), 'qqw:v';
ck qqw:v[1 $x],              (IntStr.new(1, "1"), "p", "q"), 'qqw:v: a value splits';
ck qqw:v[$n],                IntStr.new(42, "42"),    'qqw:v: one word is that word';
ck qqw:v[a $n],              ("a", IntStr.new(42, "42")), 'qqw:v: val() of an interpolated word';
{
    my @r = qqw[a $x];
    ck @r, ["a", "p", "q"],  'assigned to an array';
    ck qqw[a $x b][1], "p",  'subscripted';
    ck qqw[a $x b].elems, 4, '.elems';
}

# --- the other forms that interpolate without protection -------------------------
ck qq:w[a $x b],             ("a", "p", "q", "b"),    'qq:w';
ck q:w:s[a $x b],            ("a", "p", "q", "b"),    'q:w:s';
ck q:w:s[a \] $x],           ("a", "]", "p", "q"),    "q:w:s keeps q's escapes";
ck q:w:c [a {1 + 1} b],      ("a", "2", "b"),         'q:w:c';
ck Q:w:c[a {1 + 1} b],       ("a", "2", "b"),         'Q:w:c';
ck Qw:s[a $x b],             ("a", "p", "q", "b"),    'Qw:s';

# --- feature adverbs reach the words -----------------------------------------------
ck qqw:!s [a $x b],          ("a", "\$x", "b"),       'qqw:!s';
ck qqw:!c [a {1 + 1} b],     ("a", "\{1", "+", "1}", "b"), 'qqw:!c';
ck qqw:!b [a\tb $x],         ("a\\tb", "p", "q"),     'qqw:!b';
ck qqww:!s [a $x b],         ("a", "\$x", "b"),       'qqww:!s';
ck qqww:!c [a {1 + 1} b],    ("a", "\{1", "+", "1}", "b"), 'qqww:!c';
ck qww:s [a $x "b $x"],      ("a", "p", "q", "b p q"), 'qww:s';
ck qww:c [a {1 + 1} b],      ("a", "2", "b"),         'qww:c';

# --- delimiters ---------------------------------------------------------------------
ck qqw{a {1+1} $x},          ("a", "\{1+1}", "p", "q"), 'qqw{}: braces are text';
ck qq:w:c{a {1+1} $x},       ("a", "\{1+1}", "p", "q"), '…even under :c';
ck qqw{{a {1 + 1} $x}},      ("a", "2", "p", "q"),    'qqw{{}}: a closure';
ck qqww{{a {1 + 1} $x}},     ("a", "2", "p", "q"),    'qqww{{}}: a closure';
ck qqw｢a {1 + 1} $x｣,        ("a", "2", "p", "q"),    'qqw｢｣';
ck qqww｢a {"x y"} $x｣,       ("a", "x", "y", "p", "q"), 'qqww｢｣';

# --- qqww: code in a word is one unit ------------------------------------------------
ck qqww[a {1 + 1} b],         ("a", "2", "b"),        'qqww: a closure with blanks';
ck qqww[a {"x"} b],           ("a", "x", "b"),        'qqww: a closure holding a quote';
ck qqww[a {"x y"} b],         ("a", "x", "y", "b"),   '…and a quoted blank';
ck qqww[a { "x" } b],         ("a", "x", "b"),        '…with blanks round it';
ck qqww[a {"]"} b],           ("a", "]", "b"),        '…holding the delimiter';
ck qqww[{ 1, 2 }],            ("1", "2"),             'qqww: a list from a closure';
ck qqww[a @a.join(" ") b],    ("a", "1", "2", "b"),   'qqww: a call with a blank';
ck qqww[a $x.substr(0, 1) b], ("a", "p", "b"),        'qqww: a call with arguments';
ck qqww[a $(1 + 1) b],        ("a", "2", "b"),        'qqww: $( … )';
ck qqww[a %h<k> b],           ("a", "v", "w", "b"),   'qqww: a hash element';
ck qqww[a &f() b],            ("a", "f", "g", "b"),   'qqww: a call';
ck qqww[a\ b c],              ("a", "b", "c"),        'qqww: an escaped blank splits';
ck qqww[a\tb c],              ("a", "b", "c"),        'qqww: an escaped tab splits';
ck qqww[a "b\tc" d],          ("a", "b\tc", "d"),     '…but not in a span';
ck qqww["x y" $x],            ("x y", "p", "q"),      'qqww: a span stays whole';
ck qqww[a "b $x" c],          ("a", "b p q", "c"),    '…interpolated';
ck qqww[a{1}b],               ("a", "1", "b"),        'qqww: a word per run and per interpolation';
ck qqww[a{$x}b],              ("a", "p", "q", "b"),   '…and the value splits';

# --- «…» and <<…>> --------------------------------------------------------------------
ck «a {1 + 1} b»,             ("a", IntStr.new(2, "2"), "b"),     '«»: a closure with blanks';
ck «a {"x y"} b»,             ("a", "x", "y", "b"),               '«»: a quoted blank';
ck «a { "x" } b»,             ("a", "x", "b"),                    '«»: blanks round a quote';
ck «a {[1, 2]} b»,            ("a", IntStr.new(1, "1"), IntStr.new(2, "2"), "b"), '«»: brackets in the closure';
ck «a $x.substr(0, 1) b»,     ("a", "p", "b"),                    '«»: a call with arguments';
ck «a &f() b»,                ("a", "f", "g", "b"),               '«»: a call';
ck «a %h<k> b»,               ("a", "v", "w", "b"),               '«»: a hash element';
ck <<a {1 + 1} b>>,           ("a", IntStr.new(2, "2"), "b"),     '<<>>: a closure';
ck <<a @a.join(" ") b>>,      ("a", IntStr.new(1, "1"), IntStr.new(2, "2"), "b"), '<<>>: a call with a blank';
ck «a(b c) d»,                ("a(b", "c)", "d"),                 '«»: parens after plain text are text';

# --- a word-list form with :to ----------------------------------------------------------
ck qqw:to/END/, ("a", "p", "q", "2"), 'qqw:to';
  a $x
  {1 + 1}
  END
ck qqww:to/END/, ("a", "p", "q", "b c", "2"), 'qqww:to';
  a $x "b c"
  {1 + 1}
  END
ck qw:to/END/, ("a", "\\", "\"b", "c\""), 'qw:to';
  a \\ "b c"
  END
ck qww:to/END/, ("a", "\\", "b c"), 'qww:to';
  a \\ "b c"
  END
ck Qw:to/END/, ("a", "\\\\", "\"b", "c\""), 'Qw:to';
  a \\ "b c"
  END

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
