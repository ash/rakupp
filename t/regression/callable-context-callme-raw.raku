# Regression: three general fixes from the zef-bar campaign's first batch
# (dist test suites as the compatibility standard; zero-dep modules first).
#
# 1. `&(EXPR)` — the parenthesised Callable contextualizer — was a parse error
#    ("unexpected operator in term position"); Encode dispatches every decode
#    through `&(%encodings{$encoding})($buf)`, so the WHOLE dist failed 0/7.
#    Also accepted as a listop argument (`say &(...)()` went nullary-say).
# 2. A class with CALL-ME called BY NAME (`Trap(my $*OUT)`) threw "Undefined
#    routine" — the named-call path never consulted the class; now it
#    dispatches to CALL-ME ahead of the T(x) coercion protocol, with the
#    call's arg exprs as rwArgs so write-back works.
# 3. `is raw` params never wrote back (subs, methods, anywhere) — only `is rw`
#    did; and an explicit `C:U:` invocant param consumed a slot in the
#    rw-pairing loops, shifting every later write-back off by one.
# Together: Encode 0/7 -> 7/7 and Trap 0/2 -> 2/2 on their own suites.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. &(EXPR) in statement, assignment, and listop-argument positions
my %disp = double => sub ($n) { $n * 2 }, negate => sub ($n) { -$n };
my $r1 = &(%disp<double>)(21);
@fail.push("assign: $r1") unless $r1 == 42;
@fail.push('listop') unless "{&(%disp<negate>)(5)}" eq '-5';
my @collected;
@collected.push(&(%disp{$_})(10)) for <double negate>;
@fail.push("loop: @collected[]") unless @collected eqv [20, -10];

# 2. CALL-ME by name, plain and multi, beats coercion; coercion still works
class Adder {
    has $.base;
    multi method CALL-ME(Adder:U: $n) { $n + 1 }
    multi method CALL-ME(Adder:U: $n, $m) { $n + $m }
}
@fail.push('callme-1') unless Adder(41) == 42;
@fail.push('callme-2') unless Adder(40, 2) == 42;

# 3. is raw write-back: sub, method, multi CALL-ME with explicit invocant +
#    trailing named param (the Trap shape), and a dynamic-var argument
sub set-raw($x is raw) { $x = 'sub-ok' }
set-raw(my $a);
@fail.push("sub raw: {$a // 'undef'}") unless $a eq 'sub-ok';

class Setter {
    method fill($x is raw) { $x = 'meth-ok' }
    multi method CALL-ME(Setter:U: $x is raw, :$opt) { $x = 'callme-ok'; True }
}
Setter.fill(my $b);
@fail.push("method raw: {$b // 'undef'}") unless $b eq 'meth-ok';
Setter(my $c);
@fail.push("invocant+raw: {$c // 'undef'}") unless $c eq 'callme-ok';
Setter(my $*DYN);
@fail.push("dynamic raw: {$*DYN // 'undef'}") unless $*DYN eq 'callme-ok';

# read-only use of a raw param stays fine
sub peek($x is raw) { $x + 1 }
@fail.push('raw read') unless peek(5) == 6;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
