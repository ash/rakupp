# Deterministic corpus for the grammar-speed campaign — no RNG, so every
# regeneration is byte-identical and cross-machine comparable. Four profiles
# that stress different parts of a JSON grammar:
#
#   api.json      mid:  realistic API response — nested records, mixed types
#   deep.json     high: 48-deep alternating object/array chains
#   strings.json  high: escape-heavy — every string full of \n \" \u XXXX
#   numbers.json  mid:  long arrays of ints / decimals / exponents
#
#   rakupp gen-corpus.raku [--scale=N]     (default 1 → ~250-400 KB per file)

sub MAIN(Int :$scale = 1) {
    my $records = 400 * $scale;

    # --- api.json -----------------------------------------------------------
    my @recs;
    for ^$records -> $i {
        my $tags = (^(1 + $i % 4)).map({ '"tag' ~ ($i * 7 + $_) % 100 ~ '"' }).join(',');
        @recs.push: '{"id":' ~ $i
            ~ ',"guid":"' ~ sprintf('%08x-%04x', $i * 2654435761 % 0xFFFFFFFF, $i % 0xFFFF) ~ '"'
            ~ ',"name":"user é中' ~ $i ~ '"'
            ~ ',"active":' ~ ($i % 3 == 0 ?? 'true' !! 'false')
            ~ ',"balance":' ~ (($i * 37) % 10000) ~ '.' ~ sprintf('%02d', $i % 100)
            ~ ',"rate":' ~ (($i % 90) + 1) ~ 'e-' ~ (1 + $i % 5)
            ~ ',"notes":' ~ ($i % 5 == 0 ?? 'null' !! '"line one\nline \"two\"\ttabbed"')
            ~ ',"tags":[' ~ $tags ~ ']'
            ~ ',"address":{"street":"' ~ ($i % 999) ~ ' Main St","city":"Town' ~ ($i % 50) ~ '"'
            ~ ',"geo":{"lat":' ~ (($i * 13) % 180 - 90) ~ '.' ~ sprintf('%04d', ($i * 271) % 10000)
            ~ ',"lng":' ~ (($i * 29) % 360 - 180) ~ '.' ~ sprintf('%04d', ($i * 577) % 10000) ~ '}}'
            ~ '}';
    }
    spurt 'api.json', '{"total":' ~ $records ~ ',"page":1,"items":[' ~ @recs.join(',') ~ ']}';

    # --- deep.json ----------------------------------------------------------
    my $depth = 48;
    my @chains;
    for ^(150 * $scale) -> $i {
        my $core = '"leaf' ~ $i ~ '"';
        for ^$depth -> $d {
            $core = $d % 2 ?? '[' ~ $core ~ ',' ~ ($d * 31 + $i) ~ ']'
                           !! '{"n' ~ $d ~ '":' ~ $core ~ ',"v":' ~ ($d + $i) ~ '}';
        }
        @chains.push: $core;
    }
    spurt 'deep.json', '[' ~ @chains.join(',') ~ ']';

    # --- strings.json -------------------------------------------------------
    my @strs;
    my @hex = flat 0..9, 'a'..'f';
    for ^(1200 * $scale) -> $i {
        my $body = 'seg\u00' ~ sprintf('%02x', 0x20 + $i % 0x5e)
            ~ '\n\"quoted' ~ $i ~ '\"\t\\\\path\\\\to\\\\file'
            ~ '中文\ud83d\ude0' ~ @hex[$i % 16]
            ~ ' tail with spaces and unicode: éüñ';
        @strs.push: '"' ~ $body ~ '"';
    }
    spurt 'strings.json', '[' ~ @strs.join(',') ~ ']';

    # --- numbers.json -------------------------------------------------------
    my @nums;
    for ^(9000 * $scale) -> $i {
        @nums.push: ($i % 4 == 0) ?? ~(($i * 37) % 100000)
                 !! ($i % 4 == 1) ?? '-' ~ (($i * 13) % 1000) ~ '.' ~ sprintf('%03d', ($i * 7) % 1000)
                 !! ($i % 4 == 2) ?? (($i % 9) + 1) ~ '.' ~ ($i % 100) ~ 'e' ~ (($i % 2) ?? '-' !! '+') ~ (1 + $i % 20)
                 !!                  ~(($i * 7919) % 2**31);
    }
    spurt 'numbers.json', '[' ~ @nums.join(',') ~ ']';

    say "$_\t{$_.IO.s} bytes" for <api.json deep.json strings.json numbers.json>;
}
