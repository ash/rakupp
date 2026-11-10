# `to-json` of a structure that contains itself is refused: the writer kept
# no account of where it was, so `my @a; @a[0] = @a; to-json(@a)` wrote
# brackets until the process was killed. (JSON::Fast under Rakudo 2026.09
# does the same, and is killed the same way; this file's refusals are
# rakupp's own.) Found by the review of src/
# (docs/dev/findings/src-review-2026-11-09.md, E1).

use JSON::Fast;

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub refusal(&code) {
    code();
    return 'no refusal';
    CATCH { default { return .message.lines[0] } }
}

if $*RAKU.compiler.name eq 'Raku++' {
    my @a; @a[0] = @a;
    ck refusal({ to-json(@a) }), 'to-json: cannot write a structure that contains itself', 'an array in itself';
    my %h; %h<k> = %h;
    ck refusal({ to-json(%h) }), 'to-json: cannot write a structure that contains itself', 'a hash in itself';
    my @x; my %y = x => @x; @x[0] = %y;
    ck refusal({ to-json(@x, :!pretty) }), 'to-json: cannot write a structure that contains itself', 'a cycle of two';
}

my @b = 1, 2;
ck to-json([@b, @b], :!pretty), '[[1,2],[1,2]]', 'the same array twice, side by side, is no cycle';
ck to-json({ a => [1, { b => 2 }] }, :!pretty, :sorted-keys), '{"a":[1,{"b":2}]}', 'nesting, as before';
my @d = 1; @d = [@d] for ^50;
ck from-json(to-json(@d, :!pretty)).elems, 1, 'fifty levels deep';
ck refusal({ to-json(@b) }), 'no refusal', 'and the writer is where it started after a refusal';

say 'PASS';
