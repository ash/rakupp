# A natively compiled closure or sub answered signature introspection with the
# C++ bridge's shape, not the one the source wrote:
#
#     my &f = -> $x, $y { $x + $y };  say &f.signature.params».name;   # ($a $b)
#     sub g($a) { $a };                say &g.signature.params».name;   # ()
#
# The pointy block carried only synthetic `$^a`/`$^b` placeholders (enough for
# map/sort to count its arity), and `&g` was a bare bridge with no signature at
# all, so its arity was 0 too. Math::NIntegrate matches the integrand's
# parameter names against the range variables, so every `nintegrate` call in a
# compiled program died with "Not all integrand arguments are found in the
# ranges spec". Codegen now hands the runtime Code object the declared
# parameters (rtSig), and a placeholder block its placeholders' real names.
#
# Guarded here: interpreted and --exe both print exactly what Rakudo prints, and
# the --exe build is NATIVE — a bundle fallback runs the interpreter and would
# pass without testing the codegen at all.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what:\n  got  {$got.raku}\n  want {$want.raku}") unless $got eqv $want }

my $src = q:to/RAKU/;
    my &f = -> $x, $y { $x + $y };
    say &f.signature.params».name;
    sub g($a) { $a }
    say &g.signature.params».name;
    say &g.signature.raku, ' ', &g.name, ' ', &g.arity;
    sub h(Int $n, :$named = 3, *@rest) { }
    say &h.signature.raku, ' ', &h.arity, ' ', &h.count;
    sub r(Int $x --> Int) { $x }
    say &r.signature.raku, ' ', &r.returns.^name;
    my &p = { $^y - $^x };
    say &p.signature.params».name, ' ', &p.arity, ' ', p(1, 10);
    my &one = -> $only { $only };
    say &one.signature.raku, ' ', &one.^name, ' ', &one.signature.params».name;
    my &none = -> { 1 };
    say &none.signature.raku, ' ', &none.arity;
    my &an = sub (Str $s, $t?, :$k!) { };
    say &an.signature.raku, ' ', &an.arity, ' ', &an.count;
    say (1..6).map(-> $a, $b { $a * $b });
    say <c a b>.sort(-> $l, $r { $r cmp $l });
    say (1, 2, 3).map({ $^n * 2 });
    RAKU

# What Rakudo prints for $src, verbatim
my $want = q:to/OUT/.chomp;
    ($x $y)
    ($a)
    :($a) g 1
    :(Int $n, :$named = 3, *@rest) 1 Inf
    :(Int $x --> Int) Int
    ($x $y) 2 9
    :($only) Block ($only)
    :() 0
    :(Str $s, $t?, :$k!) 1 2
    (2 12 30)
    (c b a)
    (2 4 6)
    OUT

my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-sig-names-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $f = $dir.add('sig.raku');
$f.spurt($src);

my $i = run($bin, $f.absolute, :out, :err);
my $iout = $i.out.slurp(:close).chomp; my $ierr = $i.err.slurp(:close);
check($iout, $want, "interpreted (stderr: $ierr)");

my $exe = $dir.add('sig.bin');
my $c = run($bin, '--exe', $f.absolute, '-o', $exe.absolute, :out, :err);
my $cnote = $c.out.slurp(:close) ~ $c.err.slurp(:close);
check(so($cnote ~~ /'Compiled (native)'/), True, "--exe compiles natively (said: $cnote.trim())");
if $exe.e {
    my $r = run($exe.absolute, :out, :err);
    my $cout = $r.out.slurp(:close).chomp; my $cerr = $r.err.slurp(:close);
    check($cout, $want, "--exe (stderr: $cerr)");
}
else { @fail.push("--exe produced no binary: $cnote") }

if @fail { note "FAIL:\n" ~ @fail.join("\n"); exit 1 }
say 'PASS';
