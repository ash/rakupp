# A named placeholder (`$:name`) was treated as one more POSITIONAL parameter.
# `{ $:k ~ $^a ~ $^z }` answered `:(:$k, $a, $z)` with arity 3, where Rakudo has
# `:($a, $z, :$k!)` with arity 2: the named one is REQUIRED, comes after the
# positional ones, and counts toward neither the arity nor the count. So
# `(1..4).map({ $^a + $^b + $:x })` died "expected 3 arguments" instead of
# "Required named parameter 'x' not passed", a missing `:k` bound Any silently,
# and a surplus positional was not refused once a named placeholder was there.
#
# Rakudo lists the positional placeholders sorted and the named ones in the
# order they first appear: `{ $:c ~ $:a ~ $:b }` is `:(:$c!, :$a!, :$b!)`.
# computePlaceholders now yields that order, Callable::placeholderPos counts
# only the positional ones, the binder checks both, and a compiled block binds
# `$:name` from the named argument rather than by position.
#
# Guarded here: interpreted output is Rakudo's, refusals included; the --exe
# build (required native) agrees on everything but the refusals — compiled
# blocks do not check how many arguments they get, placeholders or not.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what:\n  got  {$got.raku}\n  want {$want.raku}") unless $got eqv $want }

my $src = q:to/RAKU/;
    my &b = { $:k ~ $^a ~ $^z };
    say &b.signature.raku, ' ', &b.arity, ' ', &b.count;
    say b(1, 2, :k<K>);
    say b(:k<K>, 1, 2);
    my &d = { $:b ~ $:a ~ $^z ~ $^y };
    say &d.signature.raku, ' ', d(1, 2, :a<A>, :b<B>);
    my &o = { $:c ~ $:a ~ $:b };
    say &o.signature.raku;
    my &c = { $:x // 'none' };
    say &c.signature.raku, ' ', &c.arity, ' ', &c.count, ' ', c(:x<X>);
    say &c.signature.params.map({ .name ~ '|' ~ .named ~ '|' ~ .optional });
    sub nm { "$:greet, $^who" }
    say &nm.signature.raku, ' ', &nm.arity, ' ', nm(:greet<Hi>, 'Bo');
    my &e = { @:list.elems + $^n };
    say &e.signature.raku, ' ', e(1, :list[1,2,3]);
    say (1..4).map({ $^a + $^b });
    RAKU

# …and what only the interpreter is held to: the refusals
my $refusals = q:to/RAKU/;
    my &b = { $:k ~ $^a ~ $^z };
    say (try b(1, :k<K>)) // "refused: $!.message()";
    say (try b(1, 2)) // "refused: $!.message()";
    say (try b(1, 2, 3, :k<K>)) // "refused: $!.message()";
    say (try (1..4).map({ $^a + $^b + $:x }).eager) // "refused: $!.message()";
    RAKU

# What Rakudo prints for each, verbatim
my $want = q:to/OUT/.chomp;
    :($a, $z, :$k!) 2 2
    K12
    K12
    :($y, $z, :$b!, :$a!) BA21
    :(:$c!, :$a!, :$b!)
    :(:$x!) 0 0 X
    ($x|True|False)
    :($who, :$greet!) 1 Hi, Bo
    :($n, :@list!) 4
    (3 7)
    OUT
my $want-refusals = q:to/OUT/.chomp;
    refused: Too few positionals passed; expected 2 arguments but got 1
    refused: Required named parameter 'k' not passed
    refused: Too many positionals passed; expected 2 arguments but got 3
    refused: Required named parameter 'x' not passed
    OUT

my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-named-ph-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

sub interp(Str $code, Str $name) {
    my $f = $dir.add("$name.raku");
    $f.spurt($code);
    my $p = run($bin, $f.absolute, :out, :err);
    my $out = $p.out.slurp(:close).chomp; my $err = $p.err.slurp(:close);
    ($out, $err, $f)
}

my ($iout, $ierr, $f) = interp($src, 'ph');
check($iout, $want, "interpreted (stderr: $ierr)");
my ($rout, $rerr) = interp($refusals, 'refuse');
check($rout, $want-refusals, "interpreted refusals (stderr: $rerr)");

my $exe = $dir.add('ph.bin');
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
