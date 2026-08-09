# Deterministic JSON corpus generator for the parse benchmark next to it.
#
# No `rand`: every run, and both engines, see byte-identical input. That matters
# more than it sounds — an A/B where the two sides parse different bytes is not
# a measurement, and a corpus that changes between runs hides small regressions
# in its own noise.
#
# The shape is what a real document has, because a parser's cost is not uniform
# across shapes: nested objects, arrays, short and long strings, ints, floats,
# and — the expensive ones — backslash and \u escapes, which are what sends
# JSON::Fast down its slow string path.
#
#     rakupp json-gen.raku --out=d800.json 800     # 278 KB, 800 records
#     for n in 200 400 800 1600 { … }              # the size ladder
sub MAIN(Int $n = 1000, Str :$out = 'doc.json') {
    my @words = <alpha bravo charlie delta echo foxtrot golf hotel india
                 juliet kilo lima mike november oscar papa quebec romeo>;
    my $fh = open $out, :w;
    $fh.print("[\n");
    for ^$n -> $i {
        my $w  = @words[$i % @words];
        my $w2 = @words[($i * 7 + 3) % @words];
        my $tags = (^5).map({ '"' ~ @words[($i + $_ * 3) % @words] ~ '"' }).join(',');
        my $desc = (^8).map({ @words[($i * 5 + $_) % @words] }).join(' ');
        $fh.print(qq:to/REC/.chomp);
          \{"id":$i,"name":"$w-$i","kind":"$w2","active":{ $i %% 3 ?? 'true' !! 'false' },
           "score":{ ($i % 997) / 7 },"count":{ $i * 31 % 100003 },
           "tags":[$tags],"desc":"$desc",
           "esc":"quote:\\" back:\\\\ tab:\\t unicode:\\u00e9 slash:\\/",
           "meta":\{"created":"2026-08-0{ $i % 9 + 1 }T12:00:00Z","depth":\{"a":\{"b":[1,2,3,{$i}]\}\}\}\}
        REC
        $fh.print($i == $n - 1 ?? "\n" !! ",\n");
    }
    $fh.print("]\n");
    $fh.close;
    say "$out: { $out.IO.s } bytes, $n records";
}
