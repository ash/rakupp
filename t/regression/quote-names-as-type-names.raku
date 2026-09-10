# Regression: an identifier that happens to spell a quoting language — Q, q, qq,
# qw, qx, m, rx, tr and the rest — is a NAME, not a quote, wherever a name is
# what the syntax calls for.
#
# `class R is Q { … }` read `Q { … }` as a Q{…} quote: the class silently lost
# its parent AND the quote swallowed the statement after the declaration, so the
# damage showed up somewhere else entirely. The same shape hit `does`, `returns`,
# `module`, `package` and `enum` (there the value list went to a Q<…> quote).
#
# The half that must NOT change is Test's `is`, spelled identically to the trait
# and followed by a term: `is q{abc}, 'abc'` is a quote and has to stay one. So
# the trait is recognised by being inside a declaration's header, and the rows
# below pin both readings.
#
# Every row uses its OWN quote-spelled name, so one name's fix cannot cover
# another's, and the "still a quote" rows use names no row declares.
#
# $rows guards the failure mode this bug HAD: a swallowed statement runs no
# check at all, which would otherwise leave the suite silently passing with
# fewer tests. The expected total is asserted at the end.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
my $rows  = 0;
sub ck($got, $want, $desc) {
    $rows++;
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- `is`: the trait, with a superclass whose name is a quote language --------
class Q { has $.x; method tag { 'Q-tag' } }
class UsesQ is Q { }
ck(UsesQ.^mro.map(*.^name).head(2).List, ('UsesQ', 'Q'), '`is Q` keeps the parent');
ck(UsesQ.new(x => 7).x, 7, 'and the inherited attribute is there');
ck(UsesQ.new.tag, 'Q-tag', 'and the inherited method');

# --- …and the statement AFTER the declaration still runs ---------------------
# This is the row the original report tripped over: the quote ate the block and
# then the next statement, so `note "reached"` never ran.
my $reached = 'no';
class AlsoQ is Q { }
$reached = 'yes';
ck($reached, 'yes', 'the statement after `class … is Q { }` is not swallowed');

# --- `does`, with a role named for a quote language --------------------------
role qq { method rtag { 'qq-role' } }
class Doesqq does qq { }
ck(Doesqq.new.rtag, 'qq-role', '`does qq` composes the role');
ck(Doesqq.new ~~ qq, True, 'and the type check knows it');

# --- `returns` ---------------------------------------------------------------
class rx { has $.n }
sub make-rx() returns rx { rx.new(n => 3) }
ck(make-rx().n, 3, '`returns rx { … }` keeps the sub body');

# --- `module` and `package` --------------------------------------------------
module tr { our sub hi { 'tr-mod' } }
ck(tr::hi(), 'tr-mod', '`module tr { … }` keeps its block');
package qx { our sub hi { 'qx-pkg' } }
ck(qx::hi(), 'qx-pkg', '`package qx { … }` keeps its block');

# --- `enum`, where the value list is angle-delimited and was read as Q<…> ----
enum mm <mm-a mm-b mm-c>;
ck(mm::<mm-b>.value, 1, '`enum mm <…>` reads a value list, not a quote');
ck(mm::<mm-c>.key, 'mm-c', 'and the key is the name');

# --- the other half: these are still QUOTES ---------------------------------
# None of the names below is declared anywhere in this file.
{
    my $s = q{plain};
    ck($s, 'plain', 'q{…} after `=` is still a quote');
    my $t = qqww{alpha beta};
    ck($t.List, ('alpha', 'beta'), 'qqww{…} is still a word-list quote');
}

# Test's `is` takes a term, and `is q{…}, …` has to keep reading as one. Spelled
# here without Test.pm so the row tests the LEXER and not the test framework: a
# sub named `is` in term position is the same parse.
{
    sub is($got, $want, $desc) { $got eq $want ?? "ok-$desc" !! "nok-$desc" }
    ck(is(q{abc}, 'abc', 'r'), 'ok-r', '`is q{abc}, …` still reads q{…} as a quote');
}

# A trait argument is parenthesised, so a quote inside one is untouched.
{
    my $v = 5 but role { method why { q{because} } };
    ck($v.why, 'because', 'a quote inside a declaration body is still a quote');
}

ck($rows, 15, 'every row above actually ran (none was swallowed)');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails ?? 1 !! 0;
