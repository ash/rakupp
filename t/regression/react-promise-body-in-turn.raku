# A react's `whenever $promise` body ran beside the react's other bodies.
#
# Rakudo runs a react's bodies one at a time. A promise that settled after its
# whenever was registered fired its body on a worker of its own, and in
# parallel mode (the default) that worker ran at once, beside the body that
# kept the promise: `$v.keep(1); log 'after keep'` could log second, and
# t/regression/promise-whenever-fires-later-and-in-turn.raku lost that race
# now and then (in 9 to 15 of 40 runs under `--exe`). A promise already kept
# when its whenever was registered ran its body inline, in the middle of the
# react's body.
#
# Now such a body waits until no other body of its react runs
# (reactPushInTurn): the one that kept the promise, the react's own body,
# another promise's. A kept promise's whenever runs after the body that
# registered it, with the react's deferred activations.
#
# Each case below loses the race every time without the fix: the body that
# keeps the promise sleeps after keeping it. This file holds the interpreter,
# the cooperative GIL (RAKUPP_GIL=1), `--exe` and `--exe -O` to Rakudo
# 2026.09's lines.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-react-promise-body-in-turn-{$*PID}");
$dir.mkdir;
LEAVE { try { my sub rm($d) { for $d.dir { $_.d ?? rm($_) !! .unlink }; $d.rmdir }; rm($dir) } }

my $src = q:to/END/;
    my @log; my $lock = Lock.new;
    sub log($x) { $lock.protect: { @log.push: $x } }

    # a body keeps the promise, then goes on for a while: the promise's body waits
    my $p = Promise.new; my $v = $p.vow;
    react {
        whenever $p { log 'promise body'; done }
        whenever Promise.in(0.05) { $v.keep(1); sleep 0.3; log 'keeper done' }
    }
    say @log.join(', ');

    # …and so it does for the react's own body
    @log = ();
    my $p2 = Promise.new; my $v2 = $p2.vow;
    react {
        whenever $p2 { log 'promise body'; done }
        $v2.keep(1); sleep 0.3; log 'react body done';
    }
    say @log.join(', ');

    # two promise bodies take turns
    @log = ();
    my $p3 = Promise.new; my $v3 = $p3.vow;
    my $p4 = Promise.new; my $v4 = $p4.vow;
    react {
        whenever $p3 { log 'p3 start'; sleep 0.2; log 'p3 end' }
        whenever $p4 { log 'p4 start'; sleep 0.2; log 'p4 end' }
        whenever Promise.in(0.05) { $v3.keep(1); $v4.keep(1) }
        whenever Promise.in(0.7) { done }
    }
    say @log.sort.join(', ');
    say 'in turn: ', so @log.join(' ') ~~ / ^ [ 'p3 start p3 end p4 start p4 end' | 'p4 start p4 end p3 start p3 end' ] $ /;

    # a promise kept before its whenever: the body after the react's own
    @log = ();
    react {
        whenever Promise.kept(5) { log "kept $_" }
        log 'react body';
    }
    say @log.join(', ');
    END

# What Rakudo 2026.09 prints for $src, line for line.
my @want =
    'keeper done, promise body',
    'react body done, promise body',
    'p3 end, p3 start, p4 end, p4 start',
    'in turn: True',
    'react body, kept 5';

sub held($what, $out) {
    my @got = $out.lines;
    for ^max(+@want, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@want[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@want[$k] // '');
    }
}

my $f = $dir.add('turn.raku');
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close)); $interp.err.slurp(:close);
my $gil = run($bin, $f.absolute, :out, :err, :env(%*ENV, RAKUPP_GIL => '1'));
held('RAKUPP_GIL=1', $gil.out.slurp(:close)); $gil.err.slurp(:close);

for (('--exe', '-O'), ('--exe',)) -> @mode {
    my $exe = $dir.add("turn-{+@mode}.bin");
    my $c = run($bin, |@mode, '-q', $f.absolute, '-o', $exe.absolute, :out, :err);
    $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
    if !$exe.e { @fail.push("{@mode} did not compile: $cerr"); next }
    my $r = run($exe.absolute, :out, :err);
    held(@mode.join(' '), $r.out.slurp(:close)); $r.err.slurp(:close);
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
