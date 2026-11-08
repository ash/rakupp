# `--> Buf` refuses a Blob, `--> Hash` a Map and `--> Rat` a FatRat, as
# Rakudo's return check does: the full check (typeOrSubsetMatches for Buf and
# Hash, rtTypeMatch's Rat case) took each sibling for the type. The fast
# return check (608774f1) accepted none of them, so only its fall-through
# answered wrongly. What each type does take still passes.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub try-it(&c) { (try c()) // $!.^name }

sub a(--> Buf) { Blob.new(1) }
sub b(--> Hash) { Map.new }
sub c(--> Rat) { FatRat.new(1, 2) }
ck try-it(&a), 'X::TypeCheck::Return', '--> Buf refuses a Blob';
ck try-it(&b), 'X::TypeCheck::Return', '--> Hash refuses a Map';
ck try-it(&c), 'X::TypeCheck::Return', '--> Rat refuses a FatRat';
try a();
ck $!.message, 'Type check failed for return value; expected Buf but got Blob (Blob.new(1))', 'the message';

sub d(--> Blob) { Buf.new(1) }
sub e(--> Map) { { a => 1 } }
sub f(--> Rational) { FatRat.new(1, 2) }
sub g(--> Buf) { buf8.new(1) }
sub h(--> Rat) { 1/2 }
sub i(--> Hash) { { a => 1 } }
sub j(--> FatRat) { FatRat.new(1, 3) }
sub k(--> Blob) { "x".encode }
ck d().list, (1,), '--> Blob takes a Buf';
ck e(), { a => 1 }, '--> Map takes a Hash';
ck f(), FatRat.new(1, 2), '--> Rational takes a FatRat';
ck g().list, (1,), '--> Buf takes a buf8';
ck h(), 1/2, '--> Rat takes a Rat';
ck i(), { a => 1 }, '--> Hash takes a Hash';
ck j(), FatRat.new(1, 3), '--> FatRat takes a FatRat';
ck k().decode, 'x', '--> Blob takes a utf8';
ck FatRat.new(1, 2) ~~ Rat, False, 'a FatRat is no Rat';

say 'PASS';
