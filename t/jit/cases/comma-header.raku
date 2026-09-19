# A C-style loop whose header is a COMMA LIST of assignments — the shape the
# Parrot-descended Mandelbrot in examples/ is written in. Two of them, nested,
# because the outer one's init is never emitted (the interpreter has already run
# it when the kernel is entered) while the inner one's IS, so the two take
# different paths through the emitter.
my $a = 0; my $b = 0; my $t = 0; my $u = 0; my $n = 0;
loop ($a = 1, $b = 2, $n = 0; $n < 400; $n++) {
    loop ($t = $n, $u = 0; $u < 40; $u++) {
        $t = $t + $a * $u - $b;
        last if $t > 100000;
    }
    $a = $a + 1;
    $b = $b + ($n % 3);
}
say "$a $b $t $u $n";
# and the step position takes a comma list too
my $p = 0; my $q = 0; my $k = 0;
loop ($k = 0; $k < 5000; $k++, $p = $p + 2) { $q = $q + $p }
say "$p $q $k";
