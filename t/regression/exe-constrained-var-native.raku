# A routine declaring a `:D`/`:U` variable, a `where`-constrained one or one
# of a subset's type ran interpreted under `--exe`, or asked the interpreter at
# every store: Codegen refused the first two (refuseDeclTraits), and a subset's
# store went to rtTypedStore, which runs the subset's `where` through the
# interpreter. `my Int:D $s = 0; for ^3_000_000 { $s += 3 }` in a sub took 0.34
# s under `--exe -O`, a `where * < 10**12` 1.19 s and a subset 0.97 s, where a
# plain `my Int $s` took 0.008 s.
#
# Now the parser gives each use of a variable its declaration's smiley and
# `where` (VarDeclInfo::readSmiley, readWhereOwn, written by the parse cache),
# and a compiled store asks them inline (Codegen::scalarStore): the `where`
# when it reads nothing but the value (a WhateverCode's body and a block's one
# expression inlined, anything else smartmatched), the smiley, then the type,
# as the interpreter asks them. A subset of a core type declared once at the
# top level with such a `where` is asked inline for a value of its base type.
# A write that would skip the check still runs interpreted: a list assignment,
# a binding, a `cas` or `⚛=`, an `is rw` argument's write-back, and a
# statement handed to the interpreter that names such a local.
#
# This file holds the interpreter, `--exe` and `--exe -O` to Rakudo 2026.09's
# lines, once as a whole program compiled natively and once bundled (a
# program-level subset bundles it, and its routines compile in module mode,
# from the parse cache's blob). It requires every routine compiled, and the
# checks in `-O`'s kept C++ inline.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-exe-constrained-var-native-{$*PID}");
$dir.mkdir;
LEAVE { try { my sub rm($d) { for $d.dir { $_.d ?? rm($_) !! .unlink }; $d.rmdir }; rm($dir) } }

my $src = q:to/END/;
    my $one = 1; my $int = Int; my $twenty = 20; my $big = 100;
    sub e(&c) { (try c()) // "died {$!.^name}" }
    sub d-eq()       { my Int:D $x = 5; $x = $int; $x }
    sub d-nil()      { my Int:D $x = 5; $x = Nil; $x }
    sub d-ok()       { my Int:D $x = 5; $x++; $x += 2; $x }
    sub d-loop()     { my Int:D $x = 0; for ^1000 { $x = $x + 1 }; $x }
    sub d-loop-bad() { my Int:D $x = 0; for ^1000 { $x = $x + 1; $x = $int if $x == 500 }; $x }
    sub u-eq()       { my Int:U $x; $x = $one; $x }
    sub u-nil()      { my Int:U $x; $x = Nil; $x.raku }
    sub u-inc()      { my Int:U $x; $x++; $x }
    sub w-star()     { my $x where * < 10 = 5; $x = $twenty; $x }
    sub w-ok()       { my $x where * < 10 = 5; $x = 7; $x += 1; $x }
    sub w-init()     { my $x where * < 10 = $twenty; $x }
    sub w-add()      { my Int $x where * < 10 = 5; $x += $big; $x }
    sub w-inc()      { my Int $x where * < 10 = 5; $x++ for ^10; $x }
    sub w-kept()     { my $x where * < 10 = 5; try { $x += $big }; $x }
    sub w-block()    { my $x where { $_ %% 2 } = 2; $x = 3; $x }
    sub w-block-ok() { my $x where { $_ %% 2 } = 2; $x += 2; $x }
    sub w-range()    { my $x where 1..10 = 1; $x = 11; $x }
    sub w-junc()     { my $x where Int|Str = 1; $x = 1.5; $x }
    sub w-junc-ok()  { my $x where Int|Str = 1; $x = "a"; $x }
    sub w-thunk()    { my $x where $_ > 0 = 1; $x = -1; $x }
    sub w-method()   { my $x where .defined = 1; $x = Any; $x }
    sub w-hot()      { my $x where * < 50000 = 1; for ^100000 { $x = $x + 1 }; $x }
    sub w-hot-ok()   { my $x where * < 500000 = 1; for ^100000 { $x = $x + 1 }; $x }
    sub dw-both()    { my Int:D $x where * < 10 = 5; $x = 50; $x }
    sub l-inner()    { my $s = 0; if $one { my Int:D $x = 5; $x = $int; $s = $x }; $s }
    sub c-closure()  { my $x where * < 10 = 5; my $f = { $x = $twenty }; $f(); $x }
    sub sh-block()   { my Int:D $x = 5; { my $x = $int; }; $x }
    sub sh-param()   { my $x where * < 10 = 5; -> $x is copy { $x = $twenty }(1); $x }
    my Int:D $t = 5;
    my $o where * < 10 = 0;
    sub o-add()      { $o += $big; $o }
    sub o-ok()       { $o += 3; $o }
    for &d-eq, &d-nil, &d-ok, &d-loop, &d-loop-bad, &u-eq, &u-nil, &u-inc, &w-star, &w-ok, &w-init, &w-add,
        &w-inc, &w-kept, &w-block, &w-block-ok, &w-range, &w-junc, &w-junc-ok, &w-thunk, &w-method, &w-hot,
        &w-hot-ok, &dw-both, &l-inner, &c-closure, &sh-block, &sh-param, &o-add, &o-ok -> &f {
        say &f.name, ' ', e(&f);
    }
    say 't ', e({ $t = $int; $t });
    say 't-ok ', e({ $t += 1; $t });
    END

# …and the subsets, after it: with them the program bundles
my $subsets = q:to/END/;
    subset Small of Int where * < 10;
    subset Even of Int where { $_ %% 2 };
    subset Pos of Int:D where * > 0;
    sub b-eq()      { my Small $x = 5; $x = $twenty; $x }
    sub b-add()     { my Small $x = 5; $x += $twenty; $x }
    sub b-ok()      { my Small $x = 5; $x--; $x }
    sub b-nil()     { my Small $x = 5; $x = Nil; $x.raku }
    sub b-str()     { my Small $x = 5; $x = "a"; $x }
    sub b-even()    { my Even $x = 2; $x = 3; $x }
    sub b-even-ok() { my Even $x = 2; $x += 2; $x }
    sub b-pos()     { my Pos $x = 1; $x = -1; $x }
    sub b-loop()    { my Small $x = 0; for ^20 { $x = $x + 1 }; $x }
    for &b-eq, &b-add, &b-ok, &b-nil, &b-str, &b-even, &b-even-ok, &b-pos, &b-loop -> &f { say &f.name, ' ', e(&f) }
    END

my $TC = 'died X::TypeCheck::Assignment';
# What Rakudo 2026.09 prints for $src, then for $subsets, line for line.
my @want =
    "d-eq $TC", "d-nil $TC", 'd-ok 8', 'd-loop 1000', "d-loop-bad $TC", "u-eq $TC", 'u-nil Int',
    "u-inc $TC", "w-star $TC", 'w-ok 8', "w-init $TC", "w-add $TC", "w-inc $TC", 'w-kept 5',
    "w-block $TC", 'w-block-ok 4', "w-range $TC", "w-junc $TC", 'w-junc-ok a', "w-thunk $TC",
    "w-method $TC", "w-hot $TC", 'w-hot-ok 100001', "dw-both $TC", "l-inner $TC", "c-closure $TC",
    'sh-block 5', 'sh-param 5', "o-add $TC", 'o-ok 3', "t $TC", 't-ok 6';
my @bwant = |@want,
    "b-eq $TC", "b-add $TC", 'b-ok 4', 'b-nil Small', "b-str $TC", "b-even $TC", 'b-even-ok 4',
    "b-pos $TC", "b-loop $TC";

sub held($what, $out, @w) {
    my @got = $out.lines;
    for ^max(+@w, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@w[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@w[$k] // '');
    }
}

my $f = $dir.add('whole.raku');
$f.spurt($src);
my $b = $dir.add('bundled.raku');
$b.spurt($src ~ $subsets);
my $interp = run($bin, $b.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close), @bwant); $interp.err.slurp(:close);

for ('whole', $f, @want), ('bundled', $b, @bwant) -> ($kind, $file, @w) {
    for (('--exe', '-O'), ('--exe',)) -> @mode {
        my $what = "$kind, {@mode}";
        my $exe = $dir.add("$kind-{+@mode}.bin");
        my $report = $dir.add("$kind-{+@mode}.report");
        my $c = run($bin, |@mode, '-q', $file.absolute, '-o', $exe.absolute, :out, :err,
                    :env(%*ENV, RAKUPP_AOT_REPORT => $report.absolute, RAKUPP_KEEPGEN => '1'));
        $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
        if !$exe.e { @fail.push("$what did not compile: $cerr"); next }
        my @r = ($report.e ?? $report.slurp !! '').lines.grep(/^ 'routine' /).map(*.split("\t"));
        if $kind eq 'whole' {
            @fail.push("$what bundled: {$cerr.lines.head}") if $cerr.contains('bundling');
        } else {
            @fail.push("$what: {+@r} routines reported, want 40") unless @r == 40;
            for @r -> @row {
                @fail.push("$what: {@row[3]} is {@row[4]}, want native") unless @row[4] eq 'native';
            }
        }
        my $r = run($exe.absolute, :out, :err);
        held($what, $r.out.slurp(:close), @w); $r.err.slurp(:close);

        # `-O`'s C++: a `where` asked by its WhateverCode's body, a smiley by
        # rtSmileyCheck, a subset's `where` for an Int without the interpreter
        next unless @mode == 2;
        my $gen = $dir.add("$kind-2.bin.rakupp.{$kind eq 'whole' ?? 'gen' !! 'stub'}.cpp");
        my $cpp = $gen.e ?? $gen.slurp !! '';
        my sub body($name) {
            my $m = $kind eq 'whole'
                ?? $cpp.match(/ 'static Value u_' $name '() {' \n <( .*? )> \n '}' \n /)
                !! $cpp.match(/ '// sub ' $name \n <( .*? )> \n '}' \n /);
            $m ?? $m.Str !! ''
        }
        my $star = body($kind eq 'whole' ?? 'w_2dstar' !! 'w-star');
        @fail.push("$what: w-star does not ask its `where` inline") unless $star.contains('RtOneArg') && $star.contains('rtLt(');
        @fail.push("$what: w-star builds a closure per store") if $star.contains('Value::closure');
        @fail.push("$what: d-eq does not ask its smiley") unless body($kind eq 'whole' ?? 'd_2deq' !! 'd-eq').contains('rtSmileyCheck(');
        if $kind eq 'bundled' {
            my $small = body('b-eq');
            @fail.push("$what: b-eq does not ask Small's `where` inline")
                unless $small.contains('rtTypedFast(') && $small.contains('rtStoreRefused(');
        }
    }
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
