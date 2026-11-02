# K6: 1-D Jacobi relaxation
my $n = 2000;
my @u = 0e0 xx $n; @u[0] = 1e0; @u[$n-1] = 1e0;
my @v = @u;
for ^400 {
    for 1..($n-2) -> $i { @v[$i] = 0.5e0 * (@u[$i-1] + @u[$i+1]) }
    for 1..($n-2) -> $i { @u[$i] = 0.5e0 * (@v[$i-1] + @v[$i+1]) }
}
say [+] @u;
