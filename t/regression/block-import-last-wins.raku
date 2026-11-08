# Two imports of one routine name in a block or a routine keep the last, as
# Rakudo's do: `{ use Other; use Other2; say run-word("a") }` called Other's
# run-word, where the mainline already kept the last. The lexical import
# stepped aside for any name the scope held, even one an earlier import put
# there; it steps aside only for a routine the scope declares itself (which
# is a redeclaration, as before), on a first load and on a module loaded
# already alike.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $dir = $*TMPDIR.add("block-import-last-{$*PID}");
$dir.mkdir;
END { run 'rm', '-rf', ~$dir if $dir.e }
for <Other Other2 Third> -> $m {
    $dir.add("$m.rakumod").spurt("unit module $m;\nsub run-word(Str \$n --> Str) is export \{ '{$m.lc}:' ~ \$n }\n");
}
my $inc = ~$dir;
sub ev(Str $code) { my $r = try EVAL "use lib '$inc'; $code"; $! ?? $!.message !! $r }

ck ev('{ use Third; use Other2; run-word("a") }'), 'other2:a', 'in a block (first loads)';
ck ev('{ use Other; use Other2; run-word("a") }'), 'other2:a', 'in a block';
ck ev('{ use Other2; use Other; run-word("a") }'), 'other:a', 'the other way round';
ck ev('sub f { use Other; use Other2; run-word("b") }; f() ~ "," ~ f()'), 'other2:b,other2:b', 'in a routine, called twice';
ck ev('{ use Other; use Other2; use Other; run-word("c") }'), 'other:c', 'three';
ck ev('{ use Other; (do { use Other2; run-word("i") }) ~ "," ~ run-word("o") }'), 'other2:i,other:o', 'an inner block';
ck ev('use Other; use Other2; run-word("m")'), 'other2:m', 'the mainline, as before';
ck ev('{ sub run-word($x) { "mine" }; use Other; run-word("e") }'), "Redeclaration of symbol '\&run-word'.",
   'a routine the block declares';

say 'PASS';
