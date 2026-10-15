# tools/str-census.raku — sums a -DRAKUPP_STR_CENSUS run and prices
# VALUE32-PLAN's designs B and C against it. How to make the input:
# tools/str-census.md.
#
#   build/rakupp tools/str-census.raku [--top=N] CENSUS [MALLOCS]
#
# CENSUS has one line per process: pid, label, then five groups of six
# length bands (empty, 1-7, 8-15, 16-22, 23-63, 64+) — strings made, copies
# of an inline string, copies of a shared body, inline texts an instance
# wrote itself, and strings made by a literal's evaluation. MALLOCS is
# tools/malloc-census.c's MALLOC_CENSUS_OUT file (pid, calls); with it the
# totals cover only the processes that have both, and each design's extra
# allocations are a share of what the program allocates today.

my @BANDS = <empty 1-7 8-15 16-22 23-63 64+>;

# What each design adds over today, from one row:
#   B  every non-empty inline text gets a body of its own
#   B' the same, but a literal shares one body kept on its AST node
#   C  only texts over 15 bytes get a body (today libc++ holds up to 22 inline)
sub designs(@r) {
    my $b  = [+] @r[19 .. 23];
    my $bl = $b - [+] @r[25 .. 27];
    my $c  = [+] @r[21 .. 23];
    $b, $bl, $c
}

sub pct($x, $of) { $of ?? sprintf('%.1f%%', 100 * $x / $of) !! '-' }

sub MAIN(Str $census, Str $mallocs?, Int :$top = 0) {
    my %calls;
    if $mallocs {
        for $mallocs.IO.lines -> $l {
            my @f = $l.split("\t");
            %calls{@f[0]} += @f[1] if @f == 2;
        }
    }
    my %rows;
    for $census.IO.lines -> $l {
        my @f = $l.split("\t");
        next unless @f == 32;
        my $r = %rows{@f[1]} //= [0 xx 31];
        $r[$_] += @f[$_ + 2] for ^30;
        $r[30] += %calls{@f[0]} // 0;
    }

    my @tot = 0 xx 31;
    my @per;   # [B%, B'%, C%, label] for each label with a malloc count
    for %rows.kv -> $label, $r {
        next if $mallocs && !$r[30];
        @tot[$_] += $r[$_] for ^31;
        if $r[30] {
            my ($b, $bl, $c) = designs($r.list);
            @per.push: [100 * $b / $r[30], 100 * $bl / $r[30], 100 * $c / $r[30], $label];
        }
    }

    say "processes: {%rows.elems}" ~ ($mallocs ?? ", {@per.elems} with a malloc count" !! '');
    say sprintf('%-22s', 'band') ~ @BANDS.map({ sprintf '%12s', $_ }).join;
    for ('made', 0), ('written inline', 18), ('literal evals', 24),
        ('copies, inline', 6), ('copies, shared', 12) -> ($name, $at) {
        say sprintf('%-22s', $name) ~ @tot[$at ..^ $at + 6].map({ sprintf '%12d', $_ }).join;
    }
    my ($b, $bl, $c) = designs(@tot);
    say '';
    say "extra allocations" ~ ($mallocs ?? ", against {@tot[30]} mallocs today" !! '') ~ ':';
    for ('B', $b, 0), ("B'", $bl, 1), ('C', $c, 2) -> ($name, $n, $i) {
        my @x = @per.map(*[$i]).sort;
        my $q = -> $f { @x ?? sprintf('%.1f%%', @x[min(@x.end, ($f * @x).Int)]) !! '-' };
        say sprintf('  %-3s %10d  %7s', $name, $n, pct($n, @tot[30]))
            ~ (@x ?? "   per process: median {$q(0.5)}, p90 {$q(0.9)}, max {$q(1)}, "
                     ~ "{+@x.grep(* > 10)} above 10%" !! '');
    }
    if $top && @per {
        say '';
        say "the $top processes B costs most:";
        for @per.sort({ -.[0] }).head($top) -> $p {
            say sprintf('  B %7.1f%%  B\' %7.1f%%  C %5.1f%%  %s', |$p);
        }
    }
}
