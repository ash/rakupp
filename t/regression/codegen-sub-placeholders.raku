# A named sub with no written signature whose body uses placeholders did not
# compile natively at all:
#
#     sub ph { $^b ~ $^a };  say ph(1, 2);   # Rakudo: 21
#
# `rakupp --exe` emitted the body reading `$^a`/`$^b` but never bound them, so
# the generated C++ referenced undeclared variables and the C++ compile failed
# ("use of undeclared identifier 'v_s_5ea'"). Blocks bound their placeholders
# (emitBlockClosure); top-level and lexical SUB bodies did not, and the sub's
# arity was 0, which under -O made it a zero-argument fast sub. Codegen now
# gives such a sub the signature its placeholders imply (positionals in sorted
# order, `$:name` named) and binds, counts and introspects it from that.
#
# Guarded here: interpreted, --exe and --exe -O each print what Rakudo prints,
# and both compiled builds are NATIVE — a bundle fallback runs the interpreter
# and would pass without testing the codegen at all.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what:\n  got  {$got.raku}\n  want {$want.raku}") unless $got eqv $want }

my $src = q:to/RAKU/;
    sub ph { $^b ~ $^a }
    say ph(1, 2);
    sub nm { "$:greet, $^who" }
    say nm('Ann', :greet<Hi>);
    sub one { $^x * 2 }
    say one(21);
    say &ph.signature.raku, ' ', &ph.arity, ' ', &ph.name;
    sub total { [+] @^xs }
    say total([1, 2, 3]);
    sub fact { $^n <= 1 ?? 1 !! $^n * fact($^n - 1) }
    say fact(10);
    sub outer($k) {
        my sub inner { $^p ~ '/' ~ $^q }
        inner($k, 'z') ~ ' ' ~ &inner.signature.raku
    }
    say outer('a');
    RAKU

# What Rakudo prints for $src, verbatim
my $want = q:to/OUT/.chomp;
    21
    Hi, Ann
    42
    :($a, $b) 2 ph
    6
    3628800
    a/z :($p, $q)
    OUT

my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-sub-ph-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $f = $dir.add('ph.raku');
$f.spurt($src);

my $i = run($bin, $f.absolute, :out, :err);
my $iout = $i.out.slurp(:close).chomp; my $ierr = $i.err.slurp(:close);
check($iout, $want, "interpreted (stderr: $ierr)");

for ('plain', (), '-O', ('-O',)) -> $label, @opt {
    my $exe = $dir.add("ph-$label.bin");
    my $c = run($bin, '--exe', |@opt, $f.absolute, '-o', $exe.absolute, :out, :err);
    my $cnote = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    check(so($cnote ~~ /'Compiled (native)'/), True, "--exe $label compiles natively (said: $cnote.trim())");
    if $exe.e {
        my $r = run($exe.absolute, :out, :err);
        my $cout = $r.out.slurp(:close).chomp; my $cerr = $r.err.slurp(:close);
        check($cout, $want, "--exe $label (stderr: $cerr)");
    }
    else { @fail.push("--exe $label produced no binary: $cnote") }
}

if @fail { note "FAIL:\n" ~ @fail.join("\n"); exit 1 }
say 'PASS';
