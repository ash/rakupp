# Regression (issue #104): a Hash or Array a PRIVATE method returns is the
# attribute's own container — `self!rec($k)<n>++` changes it, as the public
# `self.rec($k)<n>++` does. The lvalue path looked the method up without its
# `!` and wrote into a phantom attribute. Contract: exit 0 + last line PASS.
my class C {
    has %!h = x => %(n => 0);
    has @!a = 0;
    method !rec($k) { %!h{$k} }
    method !arr()   { @!a }
    method go {
        self!rec('x')<n>++;
        self!rec('x')<n> += 1;
        self!rec('x'){'n'}++;
        my $o = self;
        $o!rec('x')<n>++;
        self!arr()[0]++;
        (%!h<x><n>, @!a[0])
    }
}
my ($n, $a) = C.new.go;
say "n=$n a=$a" unless $n == 4 && $a == 1;
say $n == 4 && $a == 1 ?? 'PASS' !! 'FAIL';
