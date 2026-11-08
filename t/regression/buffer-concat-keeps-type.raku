# `~` of two buffers of one type is that type, and of any other two a plain
# Buf, as Rakudo's infix:<~>(Blob, Blob) answers: utf8 ~ utf8 was a Buf
# (Rakudo: utf8), Blob ~ Blob a Buf (Blob), a Buf beside a Buf[uint8] a
# Buf[uint8] (Buf), and a utf16 beside anything else a Str or an error
# (Buf, of the elements, each wrapped to a byte). `~=` answers the same:
# a Blob stayed no Blob, and `utf8 ~= Blob` stayed a utf8.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @v = "ab".encode, Blob.new(1), Buf.new(2), blob8.new(3), buf8.new(4), "c".encode("utf16");
my @names = (@v X @v).map(-> ($a, $b) { ($a ~ $b).^name });
ck @names, [
    'utf8', 'Buf', 'Buf', 'Buf', 'Buf', 'Buf',
    'Buf', 'Blob', 'Buf', 'Buf', 'Buf', 'Buf',
    'Buf', 'Buf', 'Buf', 'Buf', 'Buf', 'Buf',
    'Buf', 'Buf', 'Buf', 'Blob[uint8]', 'Buf', 'Buf',
    'Buf', 'Buf', 'Buf', 'Buf', 'Buf[uint8]', 'Buf',
    'Buf', 'Buf', 'Buf', 'Buf', 'Buf', 'utf16'], 'each pair';
ck ("ab".encode ~ "c".encode("utf16")).raku, 'Buf.new(97,98,99)', 'utf8 ~ utf16';
ck ("c".encode("utf16") ~ Blob.new(300)).raku, 'Buf.new(99,44)', 'a wide element wraps';
ck (buf16.new(300) ~ buf16.new(2)).raku, 'Buf[uint16].new(300,2)', 'two buf16';
ck ("a".encode ~ "b".encode).raku, 'utf8.new(97,98)', 'two utf8';
ck ("a".encode ~ "b".encode ~ "c".encode).decode, 'abc', 'which decodes';
ck ([~] Blob.new(1), Blob.new(2), Blob.new(3)).raku, 'Blob.new(1,2,3)', '[~]';
my $b = Blob.new(1); $b ~= Blob.new(2);
ck $b.raku, 'Blob.new(1,2)', 'Blob ~= Blob';
my $u = "a".encode; $u ~= "b".encode;
ck $u.raku, 'utf8.new(97,98)', 'utf8 ~= utf8';
my $k = "key".encode; $k ~= Blob.new: 0 xx 3;
ck $k.raku, 'Buf.new(107,101,121,0,0,0)', 'utf8 ~= Blob';
my $c = Buf.new(1); $c ~= buf8.new(2);
ck $c.raku, 'Buf.new(1,2)', 'Buf ~= buf8';
ck (try { my $s = "x"; $s ~= Buf.new(1); 1 }) // $!.^name, 'X::Buf::AsStr', 'a Str ~= a Buf';

say 'PASS';
