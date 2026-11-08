# A `Buf` parameter refuses a Blob, and a `Hash` parameter or variable a Map,
# as Rakudo's do: a Blob is immutable and no Buf, and a Map is no Hash, so
# `sub a(Buf $x)` took `Blob.new(1)`, `sub b(Hash $x)` took `Map.new`, and
# `my Hash $h = Map.new` held a Map. A Buf still passes for a Blob and a Hash
# for a Map, and a buffer coerces with `.Buf` and `.Blob`, which a utf8 did
# not have: `"xy".encode.Buf` is `Buf.new(120,121)`.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub a(Buf $x) { 1 }
ck (try a(Blob.new(1))) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'a Blob for Buf';
ck (a(Buf.new(1)), a(buf8.new(1))), (1, 1), 'a Buf and a buf8';
sub a2(Blob $x) { 1 }
ck (a2(Buf.new(1)), a2(Blob.new(1)), a2("x".encode)), (1, 1, 1), 'a Blob takes a Buf and a utf8';
sub a3(buf8 $x) { 1 }
ck (try a3(blob8.new(1))) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'a blob8 for buf8';
sub b(Hash $x) { 1 }
ck (try b(Map.new)) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'a Map for Hash';
ck (try b(set <a>)) // $!.^name, 'X::TypeCheck::Binding::Parameter', 'a Set for Hash';
sub b2(Map $x) { 1 }
ck (b2({a => 1}), b2(Map.new)), (1, 1), 'a Map takes a Hash';
ck (try { my Hash $h = Map.new; $h.^name }) // $!.^name, 'X::TypeCheck::Assignment', 'a Map in a Hash variable';
my Map $m = {a => 1}; ck $m.^name, 'Hash', 'a Hash in a Map variable';
ck "xy".encode.Buf.raku, 'Buf.new(120,121)', 'utf8 .Buf';
ck (Buf.new(1,2).Blob.raku, Blob.new(3).Buf.raku, Buf.new(1).Buf.raku), ('Blob.new(1,2)', 'Buf.new(3)', 'Buf.new(1)'),
   '.Blob and .Buf';
ck a("xy".encode.Buf), 1, 'a coerced Buf binds';
ck Buf.new(1,2).subbuf(0,1).^name, 'Buf', 'subbuf of a Buf';
sub h(Hash $x) { $x.^name }
ck h(Map.new((a => 1)).Hash), 'Hash', 'a Map coerced to Hash binds';

say 'PASS';
