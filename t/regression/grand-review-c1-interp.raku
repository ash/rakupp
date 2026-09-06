# Grand Review 2026-09, batch C1 (interpreter control flow, calls, numerics) —
# every case verified against Rakudo before the fix:
#
#   * a `run` that was a routine's or `do` block's VALUE threw X::Proc::Unsuccessful
#     (the Proc sink rule was not gated on sink); a `die`/`exit` inside LEAVE/KEEP/
#     UNDO was swallowed; a matched `when` in a loop body skipped NEXT/LAST and
#     dropped the iteration's value; KEEP/UNDO decided on "no exception" instead
#     of the block's value; a mainline `return` ended the program silently, exit 0.
#   * `--> T` was enforced for seven nominal types only; a `return` from CATCH
#     skipped it; `multi f(0)` matched "0" and 0e0; a named parameter's default
#     was evaluated during dispatch; a method body never sank its non-final
#     statements.
#   * a `die` inside `s/// { … }` and inside a user infix over objects was
#     swallowed; `"a" x Inf` never returned; `4.5e0 %% 2` was True; `@a is default`
#     fast path answered Any; `[...] 1, 3, 9` was 3..9; `[&&] ()` was Any;
#     `::('$nope')` carried a Str exception; `+$undef` was a Num; `>>=><<`
#     stringified the key.
#   * `5 +> -1` was 0; `abs(-2**63)` wrapped; `1e15` printed as 1e+15;
#     `NaN <=> 0` was Same; a Complex numified to 0; `(2**200).Num` was an ulp
#     off; `"ǅ".tc` uppercased the digraph.
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
sub dies(&code, $desc) {
    my $threw = False;
    try { code(); CATCH { default { $threw = True } } }
    check($threw, True, $desc);
}

# -- sink rules and phasers ----------------------------------------------------
sub run-it { run "false", :out, :err }
check(run-it().exitcode, 1, 'a run that is a routine value flows out as a Proc');
my $q = do { run "false", :out, :err };
check($q.exitcode, 1, '…and as a do block value');
sub leaver { { LEAVE { die "in-leave" } }; "after" }
my $lmsg = ''; try { leaver(); CATCH { default { $lmsg = .message } } }
check($lmsg, "in-leave", 'a die inside LEAVE propagates');
my $ex = run $*EXECUTABLE, '-e', '{ LEAVE exit 3 }; say "after"', :out, :err;
check($ex.exitcode, 3, 'exit inside LEAVE exits');
check($ex.out.slurp(:close), "", '…and the program does not run on');
my @w = do for 1..3 { when 2 { "two" }; "other" };
check(@w, ["other", "two", "other"], 'a matched when contributes its value to do-for');
my @n; for 1..3 { NEXT @n.push($_); when 2 { } }
check(@n, [1, 2, 3], 'NEXT runs after a matched when');
my $lc = 0; for 1..2 { LAST $lc++; when 2 { } }
check($lc, 1, 'LAST runs when the final iteration matched a when');
my @ku;
{ KEEP @ku.push("K"); UNDO @ku.push("U"); my $v = 5; $v }
{ KEEP @ku.push("K"); UNDO @ku.push("U"); Nil }
{ KEEP @ku.push("K"); UNDO @ku.push("U"); for 1..2 { } }
{ KEEP @ku.push("K"); UNDO @ku.push("U"); my $v = False; $v }
check(@ku, ["K", "U", "U", "K"], 'KEEP on a defined value, UNDO on Nil / an undefined result');
my $mr = run $*EXECUTABLE, '-e', 'say 1; return; say 2', :out, :err;
check($mr.exitcode, 1, 'a mainline return is an error');
check($mr.out.slurp(:close), "1\n", '…after the statements before it ran');

# -- calls and dispatch --------------------------------------------------------
sub rp(--> Positional) { 42 }
dies({ rp() }, '--> Positional rejects an Int');
sub rp2(--> Positional) { [1] }
check(rp2(), [1], '--> Positional accepts an Array');
class Kls { }
sub rk(--> Kls) { "s" }
dies({ rk() }, '--> UserClass rejects a Str');
sub rk2(--> Kls) { Kls.new }
check(rk2().^name, "Kls", '--> UserClass accepts an instance');
sub rn(--> Numeric) { "x" }
dies({ rn() }, '--> Numeric rejects a Str');
sub rn2(--> Numeric) { 1/2 }
check(rn2(), 1/2, '--> Numeric accepts a Rat');
sub rc(--> Int) { CATCH { default { return "x" } }; die "e" }
dies({ rc() }, 'a return from CATCH still passes through --> Int');
multi lit(0) { "zero" }
multi lit($x) { "other" }
check(lit(0), "zero", 'a literal parameter matches its value');
check(lit("0"), "other", '…but not a Str spelling of it');
check(lit(0e0), "other", '…nor a Num');
my $dc = 0;
multi nd(:$x = ++$dc) { $x }
nd();
check($dc, 1, 'a named default is evaluated once, at bind, not during dispatch');
my @sunk;
class Sinker { method sink { @sunk.push(1) } }
class Body { method m { Sinker.new; 42 } }
check(Body.new.m, 42, 'a method body still returns its last value');
check(@sunk.elems, 1, '…and sinks its non-final statements');

# -- swallowed exceptions --------------------------------------------------------
my $subj = "a1";
dies({ $subj ~~ s/\d/{ die "boom" }/ }, 'a die inside an s/// template block propagates');
class Pt { }
multi sub infix:<+>(Pt $a, Pt $b) { die "custom" }
my $cm = ''; try { Pt.new + Pt.new; CATCH { default { $cm = .message } } }
check($cm, "custom", 'a user infix that dies is the answer, not the built-in');

# -- operators ------------------------------------------------------------------
dies({ "a" x Inf }, '"a" x Inf is refused');
check(4.5e0 %% 2, False, 'Num %% n does not truncate the dividend');
my $dz = try { 4e0 %% 0 };
check(?$dz, False, 'Num %% 0 is not True (a Failure or an error, as on Rakudo)');
my @d is default(7); @d[3] = 1; my $j = 1;
check(@d[1], 7, 'an is-default hole answers the default on the literal fast path');
check(@d[$j], 7, '…and on the variable fast path');
check(([...] 1, 3, 9).join(","), "1,2,3,4,5,6,7,8,9", '[...] folds through the real sequence operator');
my @c;
check(([&&] @c), True, '[&&] () is True');
check(([||] @c), False, '[||] () is False');
check(([**] ()), 1, '[**] () is 1');
check(([+&] ()), -1, '[+&] () is -1');
my $nsx = ''; try { my $x = ::('$nope'); $x + 1; CATCH { default { $nsx = .^name } } }
check($nsx, 'X::NoSuchSymbol', "::('\$nope') carries a typed exception");
my $u;
check((+$u).^name, "Int", '+$undef is an Int');
check(-$u, 0, '-$undef is 0');
check(((1, 2) >>=><< (3, 4))[0].key.^name, "Int", '>>=><< keeps a non-Str key');

# -- numerics -------------------------------------------------------------------
check(5 +> -1, 10, 'a negative right-shift count shifts left');
check(5 +< -1, 2, 'a negative left-shift count shifts right');
check(abs(-9223372036854775808), 9223372036854775808, 'abs at -2**63 does not wrap');
check((-9223372036854775808).abs, 9223372036854775808, '.abs at -2**63 does not wrap');
check(1e15.Str, "1000000000000000", '1e15 prints in integer form');
check(9e15.Str, "9000000000000000", '9e15 too');
check(1e16.Str, "1e+16", '…and 1e16 does not');
check(NaN <=> 0, Nil, 'NaN <=> 0 is Nil');
check(sprintf("%d", 3+0i), "3", 'a Complex with zero imaginary part numifies');
check("abc".substr(1+0i), "bc", '…as a substr start');
check((2**200).Num == 2e0 ** 200, True, '(2**200).Num is correctly rounded');
check("ǅ".tc, "ǅ", 'titlecase of a titlecase digraph is itself');
check("ǆ".tc, "ǅ", 'titlecase of the lowercase digraph');
check("ǅemal".tc, "ǅemal", '…inside a word');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
