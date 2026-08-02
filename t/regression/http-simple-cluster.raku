# Regression: the general interpreter fixes that came out of writing
# HTTP::Simple for the ash/raku-modules repository, 2026-08-02. Both are
# language bugs the module only happened to walk into; each expectation below
# was checked against Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. `next without $x` — `without` was being read as the loop LABEL, so the
#    NextEx carried a label no loop answered to and escaped to the top level
#    ("next without loop construct"). The same went for `with`, and for `last`
#    and `redo`.
{
    my @in = 1, Int, 3, Int, 5;
    my @kept;
    for @in -> $x { next without $x; @kept.push($x) }
    ck(@kept, [1, 3, 5], 'next without $x skips the undefined ones');

    my @until-undef;
    for @in -> $x { last without $x; @until-undef.push($x) }
    ck(@until-undef, [1], 'last without $x stops at the first undefined one');

    my @defined-only;
    for @in -> $x { next with $x; @defined-only.push('gap') }
    ck(@defined-only, ['gap', 'gap'], 'next with $x keeps only the undefined ones');

    # a real label still works, and still wins
    my @pairs;
    OUTER-LOOP: for 1, 2 -> $a {
        for 10, 20 -> $b { next OUTER-LOOP if $b == 20; @pairs.push("$a-$b") }
    }
    ck(@pairs, ['1-10', '2-10'], 'a labelled next still targets its loop');
}

# 2. `Buf.new($blob)` — a Blob is Positional, and the constructor was numifying
#    it, so the buffer ended up holding the element COUNT as its single byte.
{
    my $src = Buf.new(1, 2, 3);
    ck(Blob.new($src).list.List,   (1, 2, 3), 'Blob.new(Blob) copies the bytes');
    ck(Buf.new($src).list.List,    (1, 2, 3), 'Buf.new(Blob) copies the bytes');
    # only as the SOLE argument, though — a Blob is not a uint8
    ck((try Buf.new($src, 4)).defined, False, 'a Blob among plain bytes is a type error');
    ck(Blob.new('hi'.encode('utf8')).list.List, (104, 105), 'an encoded Str keeps its bytes');
    ck(Buf.new(1, 2, 3).list.List, (1, 2, 3), 'plain byte arguments are unaffected');
}

# 3. the two together: splitting an HTTP response at the CRLFCRLF. It has to be
#    done in BYTES — in a Raku string "\r\n" is one grapheme, so a character
#    offset is out of step with the wire.
{
    my $raw = Buf.new(|"HTTP/1.1 200 OK\r\nX: 1\r\n\r\n".encode('utf8').list,
                      |"body".encode('utf8').list);
    my $sep;
    loop (my $i = 0; $i + 4 <= $raw.elems; $i++) {
        if $raw[$i] == 13 && $raw[$i+1] == 10 && $raw[$i+2] == 13 && $raw[$i+3] == 10 {
            $sep = $i; last;
        }
    }
    ck($sep.defined, True, 'the header terminator is found in the bytes');
    ck(Blob.new($raw.subbuf($sep + 4)).decode('utf8'), 'body', 'and the body survives the split');
}

say $ok ?? 'PASS' !! 'FAIL';
