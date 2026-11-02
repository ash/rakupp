my int $n = 2000;
my num @u = 0e0 xx $n; @u[0] = 1e0; @u[$n-1] = 1e0;
my num @v = @u;
loop (my int $r = 0; $r < 400; $r++) {
    loop (my int $i = 1; $i < $n-1; $i++) { @v[$i] = 0.5e0 * (@u[$i-1] + @u[$i+1]) }
    loop (my int $i = 1; $i < $n-1; $i++) { @u[$i] = 0.5e0 * (@v[$i-1] + @v[$i+1]) }
}
say [+] @u;
