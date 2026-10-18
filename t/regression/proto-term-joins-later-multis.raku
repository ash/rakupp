# Regression: a `proto` written as a TERM, its value taken, did not get the
# multis declared after it (2026-10-18). `my &up = proto sub f(|) {*}` and then
# `multi sub f(Int:D $c) {…}`: `up(49)` died with X::Multi::NoMatch, because
# the term was rewritten as `do { proto sub f(|) {*}; &f }`, so the proto was
# alone in a scope of its own and the later multis started a group outside it.
# uniprop (zef:lizmat) builds its three subs this way, inside an EVAL in a
# BEGIN block, and none of them could be called. Found while checking which
# modules need RAKUPP_VM_NAME=moar for docs/guide/faq/unknown-vm.md.
#
# The term now runs in the current scope (`stmtseq`, as an enum or subset term
# does). A proto term inside a block also has to HIDE the outer group, even
# though the block's multis are hoisted to its entry ahead of the term.
#
# Expectations checked against Rakudo v2026.09 via /opt/homebrew/bin/rakudo
# (this file prints PASS there too).

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the shape uniprop uses, written out ------------------------------------
my &up = proto sub f(|) {*}
multi sub f(Str:D $s) { "str $s" }
multi sub f(Int:D $c) { "int $c" }
ck up(49), 'int 49', 'a captured proto dispatches to the multis declared after it';
ck up('a'), 'str a', '…to each of them';
ck f(49), 'int 49', 'the name dispatches as before';
ck &up.candidates.elems, 2, 'the captured proto lists both candidates';

# --- in parentheses, as an item -------------------------------------------------
my $x = (proto sub h(|) {*});
multi sub h(Int:D $c) { $c * 2 }
ck $x(21), 42, 'a proto in parentheses, held in a scalar';
ck h(5), 10, '…and its name';

# --- inside an EVAL in a BEGIN block, exported through the variable ----------
my &uni;
BEGIN {
    Q:to/CODE/.EVAL;
    &uni = proto sub uniprop-like(|) {*}
    multi sub uniprop-like(Str:D $s, Stringy:D $p) { "str $s $p" }
    multi sub uniprop-like(Int:D $c, Stringy:D $p) { "int $c $p" }
    CODE
}
ck uni(49, 'Numeric_Value'), 'int 49 Numeric_Value', 'EVAL in BEGIN: the variable dispatches to the int candidate';
ck uni('a', 'Script'), 'str a Script', '…and to the str candidate';
ck &uni<>.candidates.elems, 2, '…which it lists';

# --- a proto term in a block hides the outer group ------------------------------
# The block's `multi sub z(Str)` is hoisted to its entry and runs before the
# term, so it starts the inner group from the outer candidates. The proto has
# to remove them again.
my $one = 1;
my $str = 'a';
proto sub z(|) {*}
multi sub z(Int) { 'outer int' }
{
    my &q = proto sub z(|) {*}
    multi sub z(Str) { 'inner str' }
    ck q('a'), 'inner str', 'a proto term in a block: its own multi';
    ck z('a'), 'inner str', '…by name too';
    ck (try z($one)) // 'hidden', 'hidden', '…and the outer candidate is hidden';
    ck &q.candidates.elems, 1, '…which the group does not list';
}
ck z(1), 'outer int', 'after the block, the outer group is unchanged';
ck (try z($str)) // 'gone', 'gone', '…and the inner candidate is gone';

# --- in a routine body, run more than once ----------------------------------------
sub mk { my &p = proto sub w(|) {*}; multi sub w(Int $n) { "w$n" }; &p }
ck mk()(1), 'w1', 'a proto term in a routine body';
ck mk()(2), 'w2', '…called again';
ck mk().candidates.elems, 1, '…with its candidate listed once, not once per call';

# --- a multi term is valued as its candidate --------------------------------------
my $c = multi sub m(Int $n) { 'm int' }
multi sub m(Str $s) { 'm str' }
ck $c(1), 'm int', 'a multi term: the candidate itself';
ck m('a'), 'm str', '…and the group gets the later multi';

say $fails ?? "FAILED $fails" !! 'PASS';
