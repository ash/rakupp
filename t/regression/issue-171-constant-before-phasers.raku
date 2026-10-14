# Issue #171: a top-level `constant` was bound only when the mainline reached
# it, but BEGIN, CHECK and INIT run before the mainline — and in Rakudo a
# constant is bound at compile time, so they see it. A module whose INIT
# called a sub reading one died "Undeclared name 'K'"; in a program every
# such phaser read the constant as Any (`constant K = 4; INIT say K` said
# (Any)). The phasers' unit now binds its constants first, in source order
# among its BEGINs, when the initializer reaches only what already exists.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
sub run-code($code, *@args) {
    my $p = run($*EXECUTABLE, |@args, '-e', $code, :out, :err);
    my $out = $p.out.slurp(:close).trim;
    my $err = $p.err.slurp(:close);
    $err ?? "$out [stderr: {$err.lines.head}]" !! $out
}

# the issue: a module's INIT calls an exported sub that reads a constant
my $dir = $*TMPDIR.add("issue171-{$*PID}");
$dir.mkdir;
$dir.add('I171.rakumod').spurt: q:to/END/;
    unit module I171;
    constant K is export = "forty-two";
    sub h(--> Str) is export { K ~ "!" }
    INIT { say h() }
    END
$dir.add('I171Chain.rakumod').spurt: q:to/END/;
    unit module I171Chain;
    constant A = 6;
    constant B = A * 7;
    my constant @L = A, B;
    sub total is export { @L.sum }
    INIT { say "init ", total() }
    say "mainline ", B;
    END
for <cold cached> -> $run {
    check run-code('use I171; say h()', '-I', ~$dir), "forty-two!\nforty-two!", "module INIT, $run run";
    check run-code('use I171Chain; say total()', '-I', ~$dir), "init 48\nmainline 42\n48",
          "module constants built from constants, $run run";
}

# a program: every phaser that runs ahead of the mainline
for Q[constant K = "fk"; sub h { K ~ "!" }; INIT { say h() }]            => 'fk!',
    Q[constant K = "fk"; INIT { say K }]                                  => 'fk',
    Q[constant K = "fk"; BEGIN { say K }]                                 => 'fk',
    Q[constant K = "fk"; CHECK { say K }]                                 => 'fk',
    Q[constant K = "fk"; sub h { K ~ "!" }; BEGIN { say h() }]            => 'fk!',
    Q[constant K = 6*7; sub h { K + 1 }; INIT { say h() }]                => '43',
    Q[my constant K = "fk"; class C { method m { K } }; INIT { say C.m }] => 'fk',
    Q[constant A = 2; constant B = A * 3; INIT { say B }]                 => '6',
    Q[sub f { 7 }; constant A = f(); INIT { say A }]                      => '7',
    Q[constant @L = 1,2,3; INIT { say @L }]                               => '(1 2 3)',
    Q[constant %H = a => 1; INIT { say %H }]                              => 'a => 1',
    Q[constant $s = 4; INIT { say $s }]                                   => '4',
    Q[BEGIN say 1; constant K = 2; BEGIN say K; say K]                    => "1\n2\n2",
    # bound once: the mainline does not run the declaration again
    Q[sub f { say "f"; 3 }; constant K = f(); INIT say K; say K]          => "f\n3\n3",
    # an initializer naming what is built later still waits for the mainline
    Q[enum E <x y>; constant K = y; say K]                                => 'y'
    -> (:key($code), :value($want)) {
    check run-code($code), $want, $code;
}

for $dir.dir { .d ?? run('rm', '-rf', ~$_) !! .unlink }
$dir.rmdir;

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
