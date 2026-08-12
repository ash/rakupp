# Benchmark BODY for the rakupp-direct side (shim concatenated in front by
# bench.py). Prints one `name ms` line per phase, best of three:
#
#   direct_*  - raw Raku: G.parse, the shim's own tree converter, $m<..> access
#   shim_*    - the same phases through rk-match-walk, still engine-side —
#               the gap between direct and shim is the walk sub, and the gap
#               between shim and a host is the ABI + the host's FFI.
#
# args: <grammar-file> <input-file>
# Compiled WITHOUT actions, so the parse number is the grammar alone.

my ($gf, $if) = @*ARGS[0], @*ARGS[1];
my $gsrc  = $gf.IO.slurp;
my $input = $if.IO.slurp;

sub best(&f) {
    my @ms;
    for ^3 {
        my $t0 = now;
        f();
        @ms.push((now - $t0) * 1000);
    }
    @ms.min
}

# ---- direct: no service at all ---------------------------------------------
my $g = EVAL($gsrc ~ ";\nLog");
my $m;
my $direct-parse = best { $m = $g.parse($input) };
die 'bench: parse failed' unless $m.defined;
my $direct-tree = best { rk-match-tree($m) };
my $n = $m<line>.elems;
my $direct-sel = best {
    for ^$n -> $i {
        my $ip = ~$m<line>[$i]<ip>;
        my $st = ~$m<line>[$i]<status>;
    }
};

# ---- through the shim's entry points ---------------------------------------
my $id = rk-grammar-compile($gsrc, 'Log', '');
my $sm;
my $shim-parse = best { $sm = rk-grammar-parse($id, $input, '') };
my $shim-tree  = best { rk-match-walk($sm, [], 'tree') };
my $shim-sel   = best {
    for ^$n -> $i {
        rk-match-walk($sm, ['line', $i, 'ip'], 'str');
        rk-match-walk($sm, ['line', $i, 'status'], 'str');
    }
};

say "lines $n";
say 'direct_parse ' ~ sprintf('%.2f', $direct-parse);
say 'direct_tree '  ~ sprintf('%.2f', $direct-tree);
say 'direct_sel '   ~ sprintf('%.2f', $direct-sel);
say 'shim_parse '   ~ sprintf('%.2f', $shim-parse);
say 'shim_tree '    ~ sprintf('%.2f', $shim-tree);
say 'shim_sel '     ~ sprintf('%.2f', $shim-sel);
