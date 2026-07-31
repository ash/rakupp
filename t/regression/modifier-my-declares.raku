# Regression: `my` under a statement modifier is a COMPILE-TIME declaration —
# the variable exists in the enclosing scope even when the modifier never runs
# the statement. `my $authority = "user" if False; $authority ~= "host"` must
# give "host" (Rakudo), not X::Undeclared (rakupp threw, GitHub report).
# Covers every modifier keyword, at file scope AND inside a sub (they took
# different code paths: hoistExprDecls vs the mainline predeclare — the
# mainline missed ALL modifier forms, blocks missed everything but if/unless),
# plus chained modifiers and a `my` assigned by a RUNNING while-modifier
# (its statement executes in the enclosing scope, so the value persists).
# Contract: exit 0 + last line PASS.
my @fail;

# the reported case, verbatim shapes
my $authority = "user" if False;
$authority ~= "host";
@fail.push("report: $authority") unless $authority eq 'host';

# skipped modifiers declare (undefined) — every keyword
my $t1 = 7 if False;      @fail.push('if')      unless ($t1 ~ 'h') eq 'h';
my $t2 = 7 unless True;   @fail.push('unless')  unless ($t2 ~ 'h') eq 'h';
my $t3 = 7 while False;   @fail.push('while')   unless ($t3 ~ 'h') eq 'h';
my $t4 = 7 until True;    @fail.push('until')   unless ($t4 ~ 'h') eq 'h';
my $t5 = 7 for ();        @fail.push('for')     unless ($t5 ~ 'h') eq 'h';
my $t6 = 7 with Nil;      @fail.push('with')    unless ($t6 ~ 'h') eq 'h';
my $t7 = 7 without 42;    @fail.push('without') unless ($t7 ~ 'h') eq 'h';
my $t8 = 7 when 42;       @fail.push('when')    unless ($t8 ~ 'h') eq 'h';

# taken modifiers still assign
my $r1 = 7 if True;       @fail.push("if-t: $r1")    unless ($r1 ~ 'h') eq '7h';
my $r2 = 7 for 1..3;      @fail.push("for-t: $r2")   unless ($r2 ~ 'h') eq '7h';
my $r3 = 7 given 9;       @fail.push("given-t: $r3") unless ($r3 ~ 'h') eq '7h';

# list/array declarations under a false modifier declare every name
my ($p1, $p2) = 1, 2 if False;
@fail.push('list-decl') unless ($p1 ~ $p2 ~ 'h') eq 'h';
my @a1 = 1, 2 if False;
@fail.push("array-decl: {@a1.elems}") unless @a1.elems == 0;

# chained modifiers hoist through both wrappers
my $c1 = 1 if False for 1..2;
@fail.push('chained') unless ($c1 ~ 'h') eq 'h';

# METHOD bodies hoist too (issue #13's follow-up: the sub path always did,
# invokeMethod never called hoistExprDecls, so a false condition inside a
# method left the variable undeclared — the reporter's URI::authority case).
class Meth {
    has $!attr;
    method report {
        my $auth = "$!attr@" if $!attr;   # attribute is undefined → skipped
        $auth ~= 'example.com';
        $auth
    }
    method taken {
        my $v = 'x' if True;
        $v ~ 'y'
    }
}
@fail.push("method skipped: {Meth.new.report}") unless Meth.new.report eq 'example.com';
@fail.push("method taken: {Meth.new.taken}")    unless Meth.new.taken eq 'xy';

# the same shapes inside a sub (block-scope hoisting path)
sub probe {
    my $s1 = 7 while False;
    my $s2 = 7 without 42;
    my $s3 = 7 when 42;
    my $s4 = 7 unless True;
    ($s1 ~ $s2 ~ $s3 ~ $s4) eq ''
}
@fail.push('sub-scope') unless probe();

# a RUNNING while-modifier executes its statement in the ENCLOSING scope, so
# the `my` value survives the loop (Rakudo: 3)
my $i = 0;
my $j = ++$i while $i < 3;
@fail.push("while-leak: {$j // 'undef'}") unless $j == 3;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
