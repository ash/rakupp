# Regression: C's conditional `cond ? a : b` RAN instead of being refused.
#
# Rakudo will not compile it: "Unsupported use of ? and : for the ternary
# conditional operator. In Raku please use: ?? and !!." A lone `?` was no
# infix to rakupp, so the expression ended at it and `? a` started a new
# statement — `my $s = $n > 2 ? "many" : "few"` warned about a useless
# constant and set $s to True. The other two shapes died of unrelated
# messages: `say $n > 2 ? 1 : 0` of "Malformed radix number" (the `: 0`), and
# `say ($n > 2 ? "many" : "few")` of "expected )". A learner pressing Run on
# the `? :` they know from C, Perl or JavaScript saw the wrong thing.
#
# Now the operator loop refuses a `?` in infix position when a `:` follows
# before the next `;`, which is the test Rakudo's infix:sym<?> makes. A
# user-declared infix:<?> keeps its meaning, and so does every `?` that is not
# in infix position. Every check verified against Rakudo.
#
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;

sub child(Str $code) {
    my $p = run($*EXECUTABLE, '-e', $code, :out, :err);
    my %r = out => $p.out.slurp(:close), err => $p.err.slurp(:close);
    %r<exit> = $p.exitcode;
    %r;
}
# Refused before anything ran, with Rakudo's message (which Rakudo breaks
# after "In", so only the part before it is looked for).
sub refuses($label, $code) {
    my %r = child($code);
    @fail.push("$label: exit={%r<exit>} out={%r<out>.raku} err={%r<err>.trim}")
        unless %r<exit> != 0 && %r<out> eq ''
               && %r<err>.contains('Unsupported use of ? and : for the ternary conditional operator');
}
# Compiled and ran to the end (each case prints its own marker last).
sub allows($label, $code, $want) {
    my %r = child($code);
    @fail.push("$label: exit={%r<exit>} out={%r<out>.raku} err={%r<err>.trim}")
        unless %r<exit> == 0 && (%r<out>.lines.tail // '') eq $want;
}

# ---- refused ---------------------------------------------------------------

# the three shapes that ran or died of something else
refuses 'in an assignment', 'say "start"; my $n = 3; my $s = $n > 2 ? "many" : "few"; say $s;';
refuses 'listop argument', 'say "start"; my $n = 3; say $n > 2 ? 1 : 0;';
refuses 'in parentheses', 'say "start"; my $n = 3; say ($n > 2 ? "many" : "few");';
# …and the spellings around them
refuses 'no spaces', 'say "start"; say 1?2:3;';
refuses 'second of two arguments', "my \$trains = 3;\nsay \"\$trains \", \$trains == 1 ? \"train\" : \"trains\";";
refuses 'inside ?? !!', 'say "start"; say 1 ?? 2 ? 3 : 4 !! 5;';
refuses 'colon on the next line', "say \"start\"; say 1 ? 2\n: 3;";
refuses 'condition of an if', 'if 1 ? 2 : 3 { say "yes" }';

# the exception is X::Obsolete, with Rakudo's attributes
try EVAL 'my $x = 0 ? 2 : 3';
@fail.push("type: {$!.^name}") unless $!.^name eq 'X::Obsolete';
@fail.push("old: {$!.old.raku}") unless $!.old eq '? and : for the ternary conditional operator';
@fail.push("replacement: {$!.replacement.raku}") unless $!.replacement eq '?? and !!';

# ---- allowed ---------------------------------------------------------------

allows '?? !!', 'my $n = 3; say $n > 2 ?? "many" !! "few";', 'many';
allows 'prefix ? in a pair', 'my %h = a => ?1, :b(?0); say %h<a>, %h<b>;', 'TrueFalse';
allows 'optional positional', 'sub f($x?) { $x // "none" }; say f();', 'none';
allows 'optional pointy parameter', 'my &g = -> $y? { $y // 0 }; say g();', '0';
allows 'optional named, return type', 'sub f(:$x?, :$y? --> Int) { 1 }; say f(:x(1));', '1';
allows 'optional in a binding', 'my ($a, $b?) := (1,); say $a;', '1';
allows '.? with colon arguments', 'class A { method m($x) { "m$x" } }; say A.new.?m: 4;', 'm4';
allows 'boolean operators', 'say (True ?| False), (True ?& False), (True ?^ True);', 'TrueFalseFalse';
allows 'regex quantifier', 'say "ac" ~~ / a b? c /;', '｢ac｣';
allows ':exists before ??', 'my %h = a => 1; say %h<a>:exists ?? "has" !! "lacks";', 'has';

# ---------------------------------------------------------------------------
if @fail {
    .say for @fail;
    say "FAIL ({@fail.elems})";
    exit 1;
}
say "PASS";
