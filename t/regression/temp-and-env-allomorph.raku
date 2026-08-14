# Regression: two bugs that between them made Test::When's suite fail 7 of 8.
#
# 1. `temp CONTAINER` was undone after the FIRST iteration of a statement-modifier
#    loop that wrote to it. A modifier loop runs its body in the CALLER's env —
#    that is how `$_` aliases without building an env per iteration — and the
#    block-exit path drained the whole pending-`temp` list, which belonged to the
#    enclosing scope. So `temp %h; %h{$_} = 7 for <A B>` restored over A, wrote B
#    into the restored container, and B then LEAKED past the scope the temp was
#    supposed to bound. Only temps a block pushed itself are unwound by it now.
#
# 2. `%*ENV` values were plain Str. Rakudo makes a value that parses as a number
#    an allomorph, so a variable set to "0" is an IntStr and therefore FALSE —
#    where a plain Str "0" is TRUE in Raku (it is not Perl). Test::When's harness
#    sets every flag it is not testing to 0 and the module asks
#    `unless %*ENV<ALL_TESTING>`; read as a Str that turned the skip off and ran
#    every test it was meant to skip.
# Contract: exit 0 + last line PASS.
my @fail;

# ---- 1. temp survives a statement-modifier loop ---------------------------
my %h = :seed(1);
sub writes-through() {
    temp %h;
    %h{$_} = 7 for <A B C>;
    %h.keys.sort.join(',');
}
my $inside = writes-through();
@fail.push("inside ($inside)") unless $inside eq 'A,B,C,seed';
@fail.push("restored ({%h.keys.sort.join(',')})") unless %h.keys.sort.join(',') eq 'seed';

# an array behaves the same
my @a = 'x';
sub push-through() { temp @a; @a.push($_) for <p q>; @a.join(',') }
@fail.push("array inside ({push-through()})") unless push-through() eq 'x,p,q';
@fail.push("array restored ({@a.join(',')})") unless @a.join(',') eq 'x';

# the block form was always right — it must stay right
my %b = :seed(1);
sub block-form() { temp %b; for <A B> { %b{$_} = 7 }; %b.keys.sort.join(',') }
@fail.push("block inside ({block-form()})") unless block-form() eq 'A,B,seed';
@fail.push("block restored ({%b.keys.sort.join(',')})") unless %b.keys.sort.join(',') eq 'seed';

# a temp in an INNER block unwinds at that block, not at the loop's first pass
my %n = :seed(1);
{
    temp %n;
    %n<outer> = 1;
    { temp %n; %n<inner> = 1; }
    @fail.push("inner-unwound ({%n.keys.sort.join(',')})") unless %n.keys.sort.join(',') eq 'outer,seed';
}
@fail.push("outer-unwound ({%n.keys.sort.join(',')})") unless %n.keys.sort.join(',') eq 'seed';

# ---- 2. %*ENV values are allomorphs ---------------------------------------
# "0" as a Str is TRUE; as the IntStr Rakudo makes it, FALSE. That difference is
# the whole point — a flag set to 0 must read as off.
{
    temp %*ENV;
    %*ENV<RAKUPP_PROBE_FLAG> = 0;
    @fail.push("env 0 truthy") if %*ENV<RAKUPP_PROBE_FLAG>;
    %*ENV<RAKUPP_PROBE_FLAG> = 1;
    @fail.push("env 1 falsy") unless %*ENV<RAKUPP_PROBE_FLAG>;
}
@fail.push('env restored') if %*ENV<RAKUPP_PROBE_FLAG>.defined;

# …and a child process sees them the same way, which is what the harness relies on
my $child = run(:out, $*EXECUTABLE, '-e',
    'print %*ENV<RAKUPP_PROBE_ZERO> ?? "true" !! "false"',
    :env(%*ENV.Hash, :RAKUPP_PROBE_ZERO(0))).out.slurp(:close);
@fail.push("child sees 0 as $child") unless $child eq 'false';

# a non-numeric value stays a plain Str
%*ENV<RAKUPP_PROBE_WORD> = 'abc';
@fail.push("word type ({%*ENV<RAKUPP_PROBE_WORD>.^name})") unless %*ENV<RAKUPP_PROBE_WORD> eq 'abc';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
