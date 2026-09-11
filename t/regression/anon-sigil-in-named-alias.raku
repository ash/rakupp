# Regression: `:filter(&)` — a named parameter whose alias is a BARE SIGIL. The
# key is accepted and its value bound to nothing, exactly as `sub f($, @)`
# declares anonymous positionals. Red's Red::ResultSeq declares an unused
# callable option that way (`multi method create-map(… :filter(&))`), and the
# whole module failed to parse.
#
# `:h(:help($))` — the same thing one alias layer down — already worked, which
# is what made the gap look like a `&`-only problem; it is not, so all four
# sigils are here, in both the untyped and the typed alias position.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape: an anonymous callable under an external name ------
{
    sub amp(:filter(&)) { 'AMP' }
    ck(amp(filter => sub { 1 }), 'AMP', 'a bare & binds nothing and the key is accepted');
    ck(amp(), 'AMP', '…and the named argument is optional, as an alias is');
}

# --- the other three sigils -----------------------------------------------
{
    sub dol(:one($))  { 'DOL' }
    sub arr(:many(@)) { 'ARR' }
    sub hsh(:opts(%)) { 'HSH' }
    ck(dol(one => 7),        'DOL', 'a bare $ under a named alias');
    ck(arr(many => (1, 2)),  'ARR', 'a bare @ under a named alias');
    ck(hsh(opts => {:a}),    'HSH', 'a bare % under a named alias');
}

# --- the TYPED alias position is a separate branch of the parser ----------
{
    sub typed(Bool :flag($)) { 'TYPED' }
    ck(typed(flag => True), 'TYPED', 'a bare sigil after a type constraint');
}

# --- nested alias layers still answer every key ---------------------------
{
    sub nested(Bool :h(:help($))) { 'NESTED' }
    ck(nested(:help),   'NESTED', 'the inner key answers');
    ck(nested(:h),      'NESTED', 'and so does the outer one');
}

# --- a NAMED variable in the same position is untouched -------------------
{
    sub named(:filter(&f)) { f() }
    ck(named(filter => sub { 'NAMED' }), 'NAMED', 'a sigilled alias still binds');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
