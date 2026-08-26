# Issue #34, fourth failure: TAP's State writes
#   proto method handle-entry(TAP::Entry $entry) { …bookkeeping…; {*}; …more… }
#   multi method handle-entry(TAP::Test $test) { … }
# A `proto` with a real body RUNS AROUND the dispatch, and the `{*}` inside it
# is where the candidates are chosen (S06).
#
# rakupp treated such a proto as an ordinary CANDIDATE, so it competed on
# signature score: for a Sub-Test argument the proto's `(Entry $e)` tied with
# `multi (Test $t)` and won, the body ran instead of the candidate, and `{*}`
# evaluated to a bare Whatever. TAP counted one test out of two.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
my @log;

role Entry { }
class Test does Entry { has $.n }
class Sub-Test is Test { }
class Plan does Entry { }

class S {
    has $.seen is rw = 0;
    proto method h(Entry $e) {
        @log.push('before');
        my $r = {*};
        $!seen++;
        @log.push('after');
        $r;
    }
    multi method h(Plan $)  { @log.push('PLAN'); 'plan' }
    multi method h(Test $t) { @log.push('TEST' ~ $t.n); 'test' }
}
my $s = S.new;
check $s.h(Test.new(:n(1))),     'test', 'a bodied proto returns the candidate value';
check $s.h(Sub-Test.new(:n(2))), 'test', 'a SUBCLASS argument reaches the same candidate';
check $s.h(Plan.new),            'plan', '…and a sibling type reaches its own candidate';
check @log.List,
      ('before', 'TEST1', 'after', 'before', 'TEST2', 'after', 'before', 'PLAN', 'after'),
      'the proto body runs around EVERY dispatch, and {*} is the dispatch point';
check $s.seen, 3, 'the proto body ran three times';

# the `{ {*} }` spelling — a pure redispatcher written with an extra brace level
class Braced {
    proto method h(Entry $e) { {*} }
    multi method h(Test $t) { 'braced-test' }
}
check Braced.h(Sub-Test.new), 'braced-test', '`{ {*} }` redispatches like a bare `{*}`';

# a proto whose group has no candidates is just the routine itself
class Alone { proto method h($x) { "alone-$x" } }
check Alone.h(7), 'alone-7', 'a bodied proto with no candidates still runs as itself';

# subs too
my @slog;
proto sub f(Entry $e) { @slog.push('p'); {*} }
multi sub f(Test $t)  { @slog.push('t'); 'sub-test' }
check f(Sub-Test.new), 'sub-test', 'a bodied proto SUB dispatches through {*}';
check @slog.List, ('p', 't'), '…running its own body first';

# and a plain multi group without any proto is unchanged
multi sub g(Int $) { 'int' }
multi sub g(Str $) { 'str' }
check g(1),   'int', 'a proto-less multi group still dispatches on type';
check g('x'), 'str', 'a proto-less multi group still dispatches on type (2)';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
