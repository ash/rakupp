# A variable's `where` was skipped in three places.
#
# The interpreter: an untyped `my $x where * < 10` in a routine or an inline
# block took a pad slot marked simple (Interpreter.cpp, collectDecls), and the
# simple-assign lane stores into one unchecked, so `$x = 101` held 101.
#
# The AST cache: the VarExpr visitor wrote neither the `where` nor that there
# was one, and a bundled program's routines are compiled from that blob. A
# compiled routine never saw the constraint, so Codegen's refusal of a
# `where`-constrained variable never fired and every store went unchecked:
# `my Int $x where * < 10 = 5; $x += 100` held 105 under `--exe`. A module
# loaded from the parse cache (`--precomp-modules`) lost it the same way.
#
# A compiled routine's store into an outer variable with a `where`: `=` asked
# the interpreter (rtOuterScalarStore), but an untyped one's `op=` and `++`
# did not, and a loop lane over one, untyped or `my Int`, wrote `.i`.
#
# Now the slot is not simple, the cache carries the `where` (v58), a routine
# that declares one runs interpreted, and a routine compiled in module mode
# checks every store into an outer name declared with a `where`
# (whereScalarNames) and gives it no lane.
#
# This file holds the interpreter, `--exe` and `--exe -O` to Rakudo 2026.09's
# lines, checks the AOT report and the kept C++ of `-O`, and runs a module
# with a `where` from the parse cache.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-exe-where-var-checked-{$*PID}");
$dir.mkdir;
LEAVE { try { my sub rm($d) { for $d.dir { $_.d ?? rm($_) !! .unlink }; $d.rmdir }; rm($dir) } }

my $src = q:to/END/;
    my $one = 1;
    sub e($c) { say (try $c()) // "died {$!.^name}" }
    my $t where * < 10 = 5;
    e({ $t = $one + 100; $t });
    e({ $t += 100; $t });
    e({ $t++ for ^20; $t });
    say $t;
    my Int $ti where * < 10 = 5;
    e({ $ti = $one + 100; $ti });
    e({ $ti += 100; $ti });
    say $ti;
    e({ my $bad where * < 10 = 50; $bad });
    sub l-eq()    { my $x where * < 10 = 5; $x = $one + 100; $x }
    sub l-add()   { my $x where * < 10 = 5; $x += 100; $x }
    sub l-inc()   { my $x where * < 10 = 5; $x++ for ^20; $x }
    sub li-eq()   { my Int $x where * < 10 = 5; $x = $one + 100; $x }
    sub li-add()  { my Int $x where * < 10 = 5; $x += 100; $x }
    sub l-loop()  { my $x where * < 10 = 1; loop (my int $k = 0; $k < 20; $k++) { $x = $x + 1 }; $x }
    sub l-while() { my Int $x where * < 10 = 1; while $x < 300 { $x += 100 }; $x }
    sub l-init()  { my $x where * < 10 = $one + 50; $x }
    sub l-kept()  { my $x where * < 10 = 5; try { $x += 100 }; $x }
    sub l-ok()    { my $x where * < 10 = 5; $x += 3; $x++; $x }
    sub l-if()    { my $s = 0; if $one { my $x where * < 10 = 5; $x = $one + 100; $s = $x }; $s }
    sub l-hot()   { my $x where * < 50000 = 1; for ^100000 { $x = $x + 1 }; $x }
    sub l-hot-ok() { my $x where * < 500000 = 1; for ^100000 { $x = $x + 1 }; $x }
    my $c where * < 10 = 0;
    my Int $d where * < 10 = 0;
    sub o-loop()  { loop (my int $k = 0; $k < 20; $k++) { $c = $c + 1 }; $c }
    sub o-add()   { $c += 100; $c }
    sub o-eq()    { $c = $one + 100; $c }
    sub o-inc()   { $c++ for ^20; $c }
    sub oi-loop() { loop (my int $k = 0; $k < 20; $k++) { $d = $d + 1 }; $d }
    sub oi-add()  { $d += 100; $d }
    for &l-eq, &l-add, &l-inc, &li-eq, &li-add, &l-loop, &l-while, &l-init, &l-kept, &l-ok, &l-if,
        &l-hot, &l-hot-ok, &o-loop, &o-add, &o-eq, &o-inc, &oi-loop, &oi-add -> &f {
        say &f.name, ' ', (try f()) // "died {$!.^name}";
    }
    say "$c $d";
    my $n = 0;
    sub count()   { loop (my int $k = 0; $k < 5; $k++) { $n = $n + 2 }; $n }
    say count();
    END

my $TC = 'died X::TypeCheck::Assignment';
# What Rakudo 2026.09 prints for $src, line for line.
my @want =
    $TC, $TC, $TC, '9',              # a top-level untyped `my $t where`: `=`, `+=`, `++`
    $TC, $TC, '5',                   # …and a `my Int $ti where`
    $TC,                             # an initializer the `where` refuses
    "l-eq $TC", "l-add $TC", "l-inc $TC", "li-eq $TC", "li-add $TC",
    "l-loop $TC", "l-while $TC", "l-init $TC",
    'l-kept 5',                      # a refused `+=` leaves the old value
    'l-ok 9',
    "l-if $TC",                      # an inline block's own `my`
    "l-hot $TC", 'l-hot-ok 100001',  # a loop long enough to tier up
    "o-loop $TC", "o-add $TC", "o-eq $TC", "o-inc $TC",   # an outer untyped `where`
    "oi-loop $TC", "oi-add $TC",     # …and an outer `my Int … where`
    '9 9',
    '10';                            # an outer `$n` with no `where`

sub held($what, $out) {
    my @got = $out.lines;
    for ^max(+@want, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@want[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@want[$k] // '');
    }
}

my $f = $dir.add('where.raku');
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close)); $interp.err.slurp(:close);

# A routine that declares a `where` runs interpreted; the ones writing an
# outer one are compiled, or the lines above say nothing about their stores.
my %native = <e o-loop o-add o-eq o-inc oi-loop oi-add count>.map(* => True);
for (('--exe', '-O'), ('--exe',)) -> @mode {
    my $exe = $dir.add("where-{+@mode}.bin");
    my $report = $dir.add("where-{+@mode}.report");
    my $c = run($bin, |@mode, '-q', $f.absolute, '-o', $exe.absolute, :out, :err,
                :env(%*ENV, RAKUPP_AOT_REPORT => $report.absolute, RAKUPP_KEEPGEN => '1'));
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

    # `-O`'s loop lane: refused for the outer `$c where`, kept for `$n`
    next unless @mode == 2;
    my $stub = $dir.add("where-2.bin.rakupp.stub.cpp");
    my $cpp = $stub.e ?? $stub.slurp !! '';
    my sub body($sub) { $cpp.match(/ '// sub ' $sub \n <( .*? )> \n '}' \n /).Str }
    @fail.push('no lane guards the outer $n') unless body('count').contains('rtIntSlot((*__fv_sn');
    @fail.push('a lane writes the outer $c where') if body('o-loop').contains('rtIntSlot');
    @fail.push('the outer $c where is stored unchecked') unless body('o-loop').contains('rtOuterScalarStore');
}

# A module with a `where`, parsed and then read from the parse cache.
my $lib   = $dir.add('lib');   $lib.mkdir;
my $cache = $dir.add('cache'); $cache.mkdir;
$lib.add('WhereVar.rakumod').spurt(q:to/MOD/);
    unit module WhereVar;
    sub w-eq() is export { my $x where * < 10 = 5; my $o = 1; $x = $o + 100; $x }
    sub w-typed() is export { my Int $x where * < 10 = 5; my $o = 1; $x += $o + 100; $x }
    MOD
my %env = %*ENV;
%env<RAKUPP_PRECOMP_DIR>     = $cache.Str;
%env<RAKUPP_PRECOMP_MODULES> = '1';
%env<RAKUPP_CONFIG>          = $dir.add('rakupp.config').Str;   # never the real one
%env<RAKUPP_NO_PRECOMP>:delete;
my $use = 'use WhereVar; say (try w-eq()) // "died {$!.^name}"; say (try w-typed()) // "died {$!.^name}"';
for 1..2 -> $run {
    my $p = run($bin, '-I', $lib.Str, '-e', $use, :env(%env), :out, :err);
    my @got = $p.out.slurp(:close).lines; $p.err.slurp(:close);
    @fail.push("module, run $run: got {@got.raku}, want two $TC") unless @got eqv [$TC, $TC];
}
my sub entries($d) { $d.dir.map({ .d ?? entries($_).Slip !! $_ }) }
@fail.push('the module left no cache entry') unless entries($cache).grep(*.Str.ends-with('.ast'));

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
