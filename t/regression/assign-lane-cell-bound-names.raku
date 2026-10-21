# Regression: `=`, `+=`, `-=`, `*=` and `~=` on a name bound to a shared cell
# take the simple-assign lane, and lose nothing on the way (review ledger O27).
#
# An `our` name is never a pad slot, nor is a `:=` alias, so an assignment to
# one went through evalAssignInner's whole path: with the loop kernels off,
# `our $n = 0; for ^400_000 { $n += 1 }` cost ~1.8k instructions a turn more
# than the same loop over a `my`, and the `-n` idiom `our $n += .chars` paid
# that on every line. The lane now takes such a name when the scope that owns
# it records nothing about it (no type, default, constraint, coercion,
# constant, bound value or rw link) — the proof a pad slot's layout gives —
# and an `our` declaration that is the target itself. Everything below is
# what the long path does that the lane must still do. Answers are Rakudo
# 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

our Int $ti = 1;
my @why;
for ^2 { try { $ti = "s" }; @why.push: $!.^name }
check @why, ['X::TypeCheck::Assignment', 'X::TypeCheck::Assignment'], 'a typed our still refuses a Str';
check $ti, 1, '…and keeps its value';
our Str $ts = "a";
for ^2 { our Str $ts; try { $ts = 5 } }
check $ts, 'a', 'a typed our redeclared in a loop still refuses';
our $dfx is default(9);
for ^2 { our $dfx; $dfx += 1 }
$dfx = Nil;
check $dfx, 9, 'Nil resets to the default';
our $nl = 5;
$nl = Nil;
check $nl, Any, 'Nil resets an untyped one to Any';

{ my $b = 1; my $a := $b; $a = 5; check $b, 5, 'an alias assigns its container'; $a += 2; check $b, 7, '…and op= does' }
{ my $b = 1; my $a := $b; $b := 7; $a = 2; check ($b, $a), (7, 2), 'a rebound source leaves the alias with the old container' }
{ my $x := 5; try { $x = 6 }; check $!.^name, 'X::AdHoc', 'a name bound to a value refuses'; check $x, 5, '…and keeps it' }
{ sub f($x is rw) { $x += 1 }; my $v = 1; f($v); check $v, 2, 'an is rw parameter' }
{ our $ox = 1; my $p := $ox; $p += 1; check $ox, 2, 'a my bound to an our' }
{ my @a = 1, 2, 3; for @a -> $x is rw { $x += 10 }; check @a, [11, 12, 13], 'an is rw loop variable' }
{ my @a = 1, 2, 3; my @w; for @a -> $x { try { $x += 10 }; @w.push: $!.^name }
  check @w, ['X::AdHoc', 'X::AdHoc', 'X::AdHoc'], 'a readonly loop variable refuses';
  check @a, [1, 2, 3], '…and the array keeps its values' }
{ my @a = 1, 2; for @a <-> $x { $x *= 3 }; check @a, [3, 6], 'a <-> loop variable' }

our $big = 2**62;
for ^4 { $big *= 2 }
check $big, 2**66, 'an Int grows past a machine word';
our $sa = "a";
for ^2 { $sa ~= "é" }
check ($sa, $sa.chars), ("aéé", 3), '~= with non-ASCII text';
our $du = Duration.new(1);
$du += Duration.new(2);
check $du.^name, 'Duration', 'a Duration stays one';
multi infix:<+>(Int $a, Int $b where * > 100) { 42 }
our $uo = 0;
$uo += 200;
check $uo, 42, "a user's infix:<+> is honoured";
our $sr = 0;
$sr += "3";
check $sr, 3, 'a Str operand numifies';
class Foo { has $.v }
multi infix:<+>(Int $a, Foo $f) { $a + $f.v * 10 }
our $ob = 1;
$ob += Foo.new(v => 2);
check $ob, 21, 'an object operand dispatches';
our $ev = 1;
EVAL q[$ev += 1];
check $ev, 2, 'from EVAL';
our $tt = 1;
sub tg { $tt }
sub tf { temp $tt; $tt += 5; tg() }
check tf(), 6, 'temp, then op=';
check $tt, 1, '…restored';
our $lst;
$lst = (1, 2);
check $lst, (1, 2), 'a List into a scalar';
our $ar;
$ar = [1, 2];
check $ar, [1, 2], 'an Array into a scalar';
our $cc = 0;
my $code = { $cc += 1 };
$code() for ^3;
check $cc, 3, 'from a closure';
our $qq = 1;
{ my $qq = 10; $qq += 1; check $qq, 11, 'a lexical my shadowing it' }
check $qq, 1, '…leaves the our alone';
for ^3 { our $dc += 1; our $ds ~= $_ }
check ((our $dc), (our $ds)), (3, '012'), 'op= on the declaration itself';
for ^3 { our $dm = $_ }
check (our $dm), 2, '= on the declaration itself';
await (^4).map: { start { for ^100 { our $par += 1 } } };
check (our $par) > 0, True, 'from several threads';

# -n keeps a total the way people write it
{
    my $in = $*TMPDIR.add("lane-cell-$*PID.txt");
    $in.spurt: (^1000).map({ "line $_" }).join("\n") ~ "\n";
    my $fh = $in.open(:r);
    my $p = run $*EXECUTABLE, '-ne', 'our $n += .chars; END say $n', :in($fh), :out;
    check $p.out.slurp(:close).trim, (^1000).map({ "line $_".chars }).sum.Str, '-n with our $n += .chars';
    $fh.close; $in.unlink;
}

# and an our's op= costs about what a my's does (rakupp only; the loop
# kernels, which take both, are switched off)
if $*RAKU.compiler.name eq 'Raku++' && !$*DISTRO.is-win {
    sub took(Str $code) {
        min (^2).map: { my $t0 = now; run(<env RAKUPP_NO_KERNELS=1>, $*EXECUTABLE, '-e', $code, :out).out.slurp(:close); now - $t0 }
    }
    my $our = took 'our $n = 0; for ^1_000_000 { $n += 1 }; say $n';
    my $my  = took 'my $n = 0; for ^1_000_000 { $n += 1 }; say $n';
    check $our < 1.8 * $my, True, "our op= within 1.8x of my ({$our.round(0.01)} s against {$my.round(0.01)} s)";
}

if @fail { note "FAILED:\n  " ~ @fail.join("\n  "); say 'FAIL' } else { say 'PASS' }
