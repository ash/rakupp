# Regression: NBSP (U+00A0), FIGURE SPACE (U+2007) and NARROW NBSP (U+202F) are
# whitespace to the LEXER — `my<NBSP>$x` declares $x, and an unspace joins
# across them — but they must NOT split a word quote: `<a<NBSP>b>` is the one
# word "a<NBSP>b", which is what makes them non-breaking.
#
# The lexer used to settle that conflict by treating all three as non-whitespace
# EVERYWHERE, so they separated nothing: `my<NBSP>@x` was a parse error, and
# S02-lexical-conventions/unicode-whitespace.t lost six assertions. The
# distinction belongs to the context, not to the character — the lexer skips
# them outside a bare `< … >` and leaves them intact inside one (gated on its
# `angleWords_` depth), which is the line uniWsLen's `breaking` argument already
# drew for the q-family blob.
#
# Every expectation below was checked against Rakudo.

use MONKEY-SEE-NO-EVAL;   # every case below is EVALd, so the lexer sees it fresh

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $BS = '\\';   # exactly one backslash, to open an unspace

# the three non-breaking spaces…
my @nb = 'NO-BREAK SPACE'        => "\x[00A0]",
         'FIGURE SPACE'          => "\x[2007]",
         'NARROW NO-BREAK SPACE' => "\x[202F]";
# …and breaking whitespace as the control: these must keep splitting words
my @br = 'SPACE'                     => "\x[0020]",
         'EN SPACE'                  => "\x[2002]",
         'OGHAM SPACE MARK'          => "\x[1680]",
         'MEDIUM MATHEMATICAL SPACE' => "\x[205F]",
         'IDEOGRAPHIC SPACE'         => "\x[3000]";

# --- every one of them separates tokens, breaking or not -------------------
for flat(@nb, @br) -> $p {
    my $c = $p.value;
    ck(EVAL('my' ~ $c ~ '$v' ~ $c ~ '=' ~ $c ~ '42;' ~ $c ~ '$v'),
       42, $p.key ~ ' separates tokens');
}

# --- the unspace joins across every one of them ----------------------------
# `foo\<ws>.lc` is `foo().lc` — 'a'. Were the whitespace visible, it would be
# the long dot `foo .lc`, i.e. foo($_.lc) — 'b'.
my $UNSP = 'multi foo() { "a" }; multi foo($x) { $x }; $_ = "b"; foo';
for flat(@nb, @br) -> $p {
    ck(EVAL($UNSP ~ $BS ~ $p.value ~ '.lc'),
       'a', 'unspace joins across ' ~ $p.key);
}

# --- a word quote does NOT break on the non-breaking three -----------------
for @nb -> $p {
    my $c = $p.value;
    ck(EVAL('<a' ~ $c ~ 'b>.elems'), 1, 'bare < > keeps one word across ' ~ $p.key);
    ck(EVAL('<a' ~ $c ~ 'b>'), 'a' ~ $c ~ 'b', '…and the word keeps the ' ~ $p.key);
    ck(EVAL('qw[a' ~ $c ~ 'b].elems'), 1, 'the q-family blob agrees for ' ~ $p.key);
    ck(EVAL('my %h; %h<a' ~ $c ~ 'b> = 5; %h.keys[0]'),
       'a' ~ $c ~ 'b', 'a subscript is one key across ' ~ $p.key);
}

# --- …while breaking whitespace still splits -------------------------------
for @br -> $p {
    my $c = $p.value;
    ck(EVAL('<a' ~ $c ~ 'b>.elems'), 2, 'bare < > still splits on ' ~ $p.key);
    ck(EVAL('qw[a' ~ $c ~ 'b].elems'), 2, 'the q-family blob splits on ' ~ $p.key);
}

# --- the reported shape, straight out of Roast -----------------------------
{
    my $c = "\x[00A0]";
    ck(EVAL('my' ~ $c ~ '@x' ~ $c ~ '=' ~ $c ~ '<a' ~ $c ~ 'b' ~ $c ~ 'c>;' ~ $c ~ '@x[0]'),
       'a' ~ $c ~ 'b' ~ $c ~ 'c',
       'the Roast case: NBSP separates the statement but not the word list');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
