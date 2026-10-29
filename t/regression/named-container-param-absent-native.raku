# Regression: an unpassed `:@a` or `:%h` parameter is an empty container of its
# type in a natively compiled routine too, as in the interpreter and Rakudo
# (issue #191).
#
# Codegen bound an unpassed named parameter to what rtNamed answers for a
# missing key, Any, whatever its sigil, so under --exe `sub MAIN(Str
# :m(:@mode))` given no --mode had Any for @mode, and `@mode.push` died; `:%h`
# was Any too, and `.elems` of either was 1. It binds the empty container a `my
# Str @a` or `my %h` starts as now. t/run.raku compiles this file with --exe and
# holds it to native.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub arr(:@a) { @a }
sub typed(Str :@a) { @a }
sub hsh(:%h) { %h }
sub typed-hash(Int :%h) { %h }
sub renamed(:n(:@nums)) { @nums.elems }
sub given(:@a) { @a.elems }

ck arr().raku, '[]', 'an unpassed :@a is []';
ck arr().elems, 0, '…with no elements';
ck typed().raku, 'Array[Str].new()', 'an unpassed Str :@a is Array[Str].new()';
ck hsh().raku, '{}', 'an unpassed :%h is {}';
ck hsh().elems, 0, '…with no pairs';
ck typed-hash().raku, '(my Int %)', 'an unpassed Int :%h is a Hash[Int]';
ck renamed(), 0, 'an unpassed :n(:@nums) is empty';
ck renamed(:n(1, 2)), 2, '…and :n binds it';
ck renamed(:nums(1, 2, 3)), 3, '…and :nums too';
ck given(:a(1, 2)), 2, 'a passed :@a is what was passed';

sub MAIN(Str :m(:@mode), Str :$o, :%opts) {
    ck @mode.raku, 'Array[Str].new()', 'MAIN with no --mode has an empty Array[Str]';
    @mode.push('x');
    ck @mode.raku, 'Array[Str].new("x")', '…that takes a push';
    ck %opts.elems, 0, 'MAIN with no --opts has an empty hash';
    say 'PASS';
}
