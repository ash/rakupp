# A variable's `:D` or `:U` was skipped in three places, and the parse cache
# dropped it with five more of a declaration's fields.
#
# The parse cache: the VarExpr visitor wrote neither the smiley nor the
# coercion's source type (`my Int(Str)`), `(state @a) = …`'s every-time
# assignment, `my constant`, `my ::foo $x`'s type name or that a declaration
# stood bare. A module read from the cache took any value into a `my Int:D $x`,
# a Rat into a `my Int(Str) $x`, assigned `(state @a) = 1, 2` once, and made a
# `my constant` a package symbol. A bundled program's routines are compiled
# from that blob, so Codegen's refusal of a `:D`/`:U` variable never fired and
# `my Int:D $x = 5; $x = Int` stored the type object under `--exe`.
#
# The interpreter: `=` checked the smiley, but a `cas`, an `⚛=`, an `op=`, a
# `//=` and a `++` did not (enforceTypedAssign), so `my Int:U $x; $x++` held 1.
#
# A compiled routine's store into an outer `my Int:D $c` asked the interpreter
# (rtOuterScalarStore), which checked the type and not the smiley, and reset a
# Nil to the type object.
#
# This file holds the interpreter, `--exe` and `--exe -O` to Rakudo 2026.09's
# lines, checks the AOT report, and runs a module twice from the parse cache.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-smiley-var-checked-{$*PID}");
$dir.mkdir;
LEAVE { try { my sub rm($d) { for $d.dir { $_.d ?? rm($_) !! .unlink }; $d.rmdir }; rm($dir) } }

my $src = q:to/END/;
    my $one = 1;
    my $five = 5;
    my $int = Int;
    sub e(&c) { (try c()) // "died {$!.^name}" }
    sub d-eq()     { my Int:D $x = 5; $x = $int; $x.raku }
    sub d-add()    { my Int:D $x = 5; $x += $one; $x = $int; $x.raku }
    sub d-nil()    { my Int:D $x = 5; $x = Nil; $x.raku }
    sub d-dflt()   { my Int:D $x is default(3) = 5; $x = Nil; $x.raku }
    sub d-arr()    { my Int:D @a = 1, 2; @a[0] = $int; @a.raku }
    sub d-loop()   { my Int:D $x = 0; loop (my int $k = 0; $k < 10; $k++) { $x = $x + 1 }; $x = $int; $x.raku }
    sub d-ok()     { my Int:D $x = 5; $x += 3; $x++; $x }
    sub d-cas()    { my Int:D $x = 1; cas($x, 1, $int); $x.raku }
    sub d-atomic() { my Int:D $x = 1; $x ⚛= $int; $x.raku }
    sub u-eq()     { my Int:U $x; $x = $five; $x.raku }
    sub u-inc()    { my Int:U $x; $x++; $x.raku }
    sub u-add()    { my Int:U $x; $x += 1; $x.raku }
    sub u-or()     { my Int:U $x; $x //= 5; $x.raku }
    sub u-ok()     { my Int:U $x; $x = Int; $x = Nil; $x.raku }
    my Int:D $c = 0;
    my Int:U $u;
    sub o-eq()     { $c = $int; $c.raku }
    sub o-nil()    { $c = Nil; $c.raku }
    sub o-loop()   { loop (my int $k = 0; $k < 10; $k++) { $c = $c + 1 }; $c = $int; $c.raku }
    sub o-ok()     { $c += 2; $c }
    sub ou-eq()    { $u = $five; $u.raku }
    sub ou-nil()   { $u = Nil; $u.raku }
    for &d-eq, &d-add, &d-nil, &d-dflt, &d-arr, &d-loop, &d-ok, &d-cas, &d-atomic,
        &u-eq, &u-inc, &u-add, &u-or, &u-ok, &o-eq, &o-nil, &o-loop, &o-ok, &ou-eq, &ou-nil -> &f {
        say &f.name, ' ', e(&f);
    }
    my Int:D $t = 5;
    say 't ', e({ $t = $int; $t.raku });
    say "c $c";
    END

my $TC = 'died X::TypeCheck::Assignment';
# What Rakudo 2026.09 prints for $src, line for line.
my @want =
    "d-eq $TC", "d-add $TC", "d-nil $TC",
    'd-dflt 3',                      # a Nil resets to `is default`
    "d-arr $TC", "d-loop $TC", 'd-ok 9', "d-cas $TC", "d-atomic $TC",
    "u-eq $TC", "u-inc $TC", "u-add $TC", "u-or $TC", 'u-ok Int',
    "o-eq $TC", "o-nil $TC", "o-loop $TC", 'o-ok 12',   # an outer `my Int:D`
    "ou-eq $TC", 'ou-nil Int',                          # …and `my Int:U`
    "t $TC",
    'c 12';

sub held($what, $out, @w = @want) {
    my @got = $out.lines;
    for ^max(+@w, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@w[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@w[$k] // '');
    }
}

my $f = $dir.add('smiley.raku');
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close)); $interp.err.slurp(:close);

# A routine that declares a `:D` or `:U` runs interpreted; the ones writing an
# outer one are compiled, or the lines above say nothing about their stores.
my %native = <e o-eq o-nil o-loop o-ok ou-eq ou-nil>.map(* => True);
for (('--exe', '-O'), ('--exe',)) -> @mode {
    my $exe = $dir.add("smiley-{+@mode}.bin");
    my $report = $dir.add("smiley-{+@mode}.report");
    my $c = run($bin, |@mode, '-q', $f.absolute, '-o', $exe.absolute, :out, :err,
                :env(%*ENV, RAKUPP_AOT_REPORT => $report.absolute));
    $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
    if !$exe.e { @fail.push("{@mode} did not compile: $cerr"); next }
    my @r = ($report.e ?? $report.slurp !! '').lines.grep(/^ 'routine' /).map(*.split("\t"));
    @fail.push("{@mode}: {+@r} routines reported, want 21") unless @r == 21;
    for @r -> @row {
        my $name = @row[3].subst(/^ 'sub '/, '');
        my $how = %native{$name} ?? 'native' !! 'interpreted';
        @fail.push("{@mode}: $name is {@row[4]}, want $how") unless @row[4] eq $how;
    }
    my $r = run($exe.absolute, :out, :err);
    held(@mode.join(' '), $r.out.slurp(:close)); $r.err.slurp(:close);
}

# A module, parsed and then read from the parse cache.
my $lib   = $dir.add('lib');   $lib.mkdir;
my $cache = $dir.add('cache'); $cache.mkdir;
$lib.add('Smiley.rakumod').spurt(q:to/MOD/);
    unit module Smiley;
    sub e(&c) is export { (try c()) // "died {$!.^name}" }
    sub m-d() is export { my $int = Int; my Int:D $x = 5; $x = $int; $x.raku }
    sub m-u() is export { my $five = 5; my Int:U $x; $x = $five; $x.raku }
    sub m-arr() is export { my $int = Int; my Int:D @a = 1, 2; @a[0] = $int; @a.raku }
    sub m-coerce() is export { my $r = 5.5; my Int(Str) $x = $r; $x.raku }
    sub m-coerce-ok() is export { my Int(Str) $x = "7"; $x.raku }
    sub m-state() is export { (state @s) = 1, 2; @s.push(3); @s.elems }
    my constant MC = 42;
    sub m-const() is export { MC }
    subset S of Str:D(Rat);
    sub m-stub() is export { my ::foo $x; $x.^name }
    sub m-bare() is export { my S $s; $s.raku }
    MOD
my @mwant =
    "m-d $TC", "m-u $TC", "m-arr $TC", "m-coerce $TC", 'm-coerce-ok 7',
    'm-state 3 3',                    # `(state @s) = …` assigns every call
    'm-const 42 not a package symbol',
    # …and a fresh parse's own lines: Rakudo shows this `$x` as NQPMu, and
    # refuses the bare `my S $s` at compile time
    'm-stub foo', 'm-bare died X::Syntax::Variable::MissingInitializer';
my %env = %*ENV;
%env<RAKUPP_PRECOMP_DIR>     = $cache.Str;
%env<RAKUPP_PRECOMP_MODULES> = '1';
%env<RAKUPP_CONFIG>          = $dir.add('rakupp.config').Str;   # never the real one
%env<RAKUPP_NO_PRECOMP>:delete;
my $use = q:to/USE/;
    use Smiley;
    for &m-d, &m-u, &m-arr, &m-coerce, &m-coerce-ok -> &f { say &f.name, ' ', e(&f) }
    say 'm-state ', m-state(), ' ', m-state();
    say 'm-const ', m-const(), ' ', (try Smiley::MC) // 'not a package symbol';
    say 'm-stub ', m-stub();
    say 'm-bare ', e(&m-bare);
    USE
for 1..2 -> $run {
    my $p = run($bin, '-I', $lib.Str, '-e', $use, :env(%env), :out, :err);
    held("module, run $run", $p.out.slurp(:close), @mwant); $p.err.slurp(:close);
}
my sub entries($d) { $d.dir.map({ .d ?? entries($_).Slip !! $_ }) }
@fail.push('the module left no cache entry') unless entries($cache).grep(*.Str.ends-with('.ast'));

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
