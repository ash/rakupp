# Regression: `my %` and `my &` died the moment the declaring block RAN —
#   Variable %!anon used where no 'self' is available
# The anonymous slot was named `%!anon`, and that second character is the
# PRIVATE-ATTRIBUTE twigil, so evaluating the declaration (which a block does,
# for its value) fell into the attribute path and asked for a `self` that is not
# there. `my $` and `my @` were fine — the lexer hands those to the named
# branch, and only `%` and `&`, the two sigils that double as operators, reached
# the anonymous one. It cost S02-names/bare-sigil.t, which passed 10 of 11.
#
# The slot is now named with the house `\x01` sentinel, which no source text can
# spell — so it is out of reach of the twigil test AND of a bare `%` term, which
# in Rakudo is a FRESH hash rather than the anonymous variable beside it.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape: the declaration is evaluated, so it must live -------
ck((do { my %; "ok" }), "ok", 'my % inside a block lives');
ck((do { my &; "ok" }), "ok", 'my & inside a block lives');
ck((do { my @; "ok" }), "ok", 'my @ still does');
ck((do { my $; "ok" }), "ok", 'and my $');
ck((do { my Int %; "ok" }), "ok", 'a typed anonymous hash lives');
ck((do { state %;   "ok" }), "ok", 'state % lives');
ck((do { my %{Str}; "ok" }), "ok", 'an anonymous object hash lives');
ck((do { my %; my %; "ok" }), "ok", 'two in one scope live');
ck((sub { my % }() ~~ Hash), True, 'and a sub body is no different');

# --- the declaration still yields the container it declared -----------------
ck((do { my % }).^name, 'Hash',  'the value of `my %` is a Hash');
ck((do { my @ }).^name, 'Array', 'of `my @`, an Array');
ck((do { my $ }).^name, 'Any',   'of `my $`, an undefined Any');

# --- an initializer still reaches the container -----------------------------
ck((my % = (a => 1)).raku, '{:a(1)}', 'my % = … assigns');
ck((my @ = 1, 2).raku,     '[1, 2]',  'my @ = … assigns');

# --- and the slot is UNNAMEABLE: a bare `%` term is a fresh hash ------------
ck((%).raku, '{}', 'a bare % term does not find the anonymous variable above it');
ck((% .classify-list({ $_ %% 2 })).raku, '{}', 'the bare % term still classifies into itself');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
