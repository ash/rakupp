# Regression (RAKUDO-ISSUES-PLAN R3; rakudo#3292): assigning to a sigilless
# name bound to an object with a STORE method calls that STORE, as `@a = …`
# assigns through a Positional's.
#
# Before the fix `my \foo = HistoryVar.new; foo = 1` died "Cannot modify an
# immutable HistoryVar": the parser had turned the assignment into a refusal
# and dropped the right-hand side. An object without STORE, a value and a
# type object are still refused, after the right-hand side has run, as in
# Rakudo.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub refused(Str $what, &code, Str $msg) {
    my $got = 'nothing';
    try { code(); CATCH { default { $got = .message } } }
    check $what, $got, $msg;
}

class HistoryVar {
    has @.history;
    method STORE($v) { @!history.push($v); self }
}
my \foo = HistoryVar.new;
foo = 1;
foo = 42;
check 'STORE takes each assignment (rakudo#3292)', foo.history, [1, 42];
check 'a scalar variable is replaced, not STOREd into', do { my $o = HistoryVar.new; $o = 1; $o }, 1;

class Plain { }
my \p = Plain.new;
refused 'an object without STORE', { p = 1 }, 'Cannot modify an immutable Plain (Plain.new)';
my \x = 5;
my $ran = False;
refused 'a value', { x = ($ran = True) }, 'Cannot modify an immutable Int (5)';
check '…after the right-hand side ran', $ran, True;
my \t = Int;
refused 'a type object', { t = 1 }, "Cannot modify an immutable 'Int' type object";

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
