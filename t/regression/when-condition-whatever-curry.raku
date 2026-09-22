# Regression: a `when` condition containing `*` took the WRONG ARM when the
# program was native-compiled. `--exe` emitted `applyArith("~~", $topic, COND)`
# for every `when`, and that is not what `when` means:
#
#   * `* < 1` evaluates to a WhateverCode, and handing one to applyArith curried
#     it a SECOND time. The result was a Code object — truthy whatever the topic
#     — so the FIRST arm always won.
#   * `*.chars == 3` and `!(* < 1)` curry in the interpreter's evaluator, not at
#     value level, so emitted code computed them against the Whatever itself.
#
# Reported as issue #96: `given @a[$i] - @a[$i-1] { when * < 1 {…} when * >= 1
# {…} }` printed "Hello" four times interpreted (as Rakudo does) and "world"
# four times compiled. Fixed by giving the compiled `when` the interpreter's own
# rule — rtWhenMatch — and currying a `*`-bearing condition the way an argument
# is curried.
#
# A CATCH block's when-chain is emitted by the same code and took the same fix,
# but it is deliberately NOT exercised here: a CATCH makes --exe bundle the whole
# program with the interpreter, so one such case in this file would cost the
# other twenty-two their native coverage in the differential gate.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
# The arm a given/when chain takes, as a string.
sub arm($topic, &chain) { chain($topic) }

# --- the reported shape ---------------------------------------------------
my @a = ('1', '2', '3', '4', '5');
my @arms;
for 1..4 -> $i {
    given @a[$i] - @a[$i-1] {
        when * <  1 { @arms.push: 'world' }
        when * >= 1 { @arms.push: 'Hello' }
    }
}
ck(@arms, ['Hello', 'Hello', 'Hello', 'Hello'], 'issue #96: a curried comparison picks the arm that is TRUE');

# --- one curried condition per shape --------------------------------------
sub lt1($t) { given $t { when * < 1 { 'lt' }; default { 'ge' } } }
ck(lt1(3),  'ge', 'a false curried comparison falls through');
ck(lt1(0),  'lt', '…and a true one matches');

sub even($t) { given $t { when * %% 2 { 'even' }; default { 'odd' } } }
ck(even(5), 'odd',  'a false curried divisibility falls through');
ck(even(4), 'even', '…and a true one matches');

sub three($t) { given $t { when *.chars == 3 { 'three' }; default { 'other' } } }
ck(three('abc'),  'three', 'a method call on `*` curries');
ck(three('abcd'), 'other', '…and answers False when it should');

sub plus($t) { given $t { when * + 1 { 'truthy' }; default { 'falsy' } } }
ck(plus(3),  'truthy', 'a curried sum is matched by its truth');
ck(plus(-1), 'falsy',  '…so a sum of 0 does NOT match');

sub negated($t) { given $t { when !(* < 1) { 'not' }; default { 'nonot' } } }
ck(negated(3), 'not',   'a prefix op over `*` curries');
ck(negated(0), 'nonot', '…and answers False when it should');

sub band($t) { given $t { when * > 2 && * < 5 { 'in' }; default { 'out' } } }
ck(band(3), 'in',  'two curried comparisons under &&');
ck(band(9), 'out', '…and the falling-through side');

# --- a BARE `*` is a value, not a curry -----------------------------------
# `when *` matches anything — it is `default` spelled with a star — so currying
# it into a one-argument identity closure (answering the TOPIC's truth) is wrong.
sub star($t) { given $t { when * { 'star' }; default { 'nostar' } } }
ck(star(0),     'star', 'a bare `*` matches a falsy topic');
ck(star('abc'), 'star', '…and any other');

# --- what must keep working -----------------------------------------------
sub plain($t) {
    given $t {
        when 3     { 'literal' }
        when 1..2  { 'range'   }
        when Str   { 'Str'     }
        when /^z/  { 'regex'   }
        when { $_ ~~ Int && $_ > 100 } { 'block' }
        default    { 'default' }
    }
}
ck(plain(3),     'literal', 'a literal condition');
ck(plain(2),     'range',   'a range condition');
ck(plain('zoo'), 'Str',     'a type condition (Str beats the regex arm, being written first)');
ck(plain(999),   'block',   'a block condition');
ck(plain(7),     'default', 'and the default');
sub stored($t) {
    my $pred = * < 1;                     # a WhateverCode in a VARIABLE
    given $t { when $pred { 'lt' }; default { 'ge' } }
}
ck(stored(3), 'ge', 'a stored WhateverCode is called, not re-curried');
ck(stored(0), 'lt', '…and matches when it answers True');
{
    my @seen;
    given 'a0b' { when /(\d)/ { @seen.push: ~$0 }; default { @seen.push: 'no' } }
    ck(@seen, ['0'], 'a regex literal still matches the topic and publishes $/');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
