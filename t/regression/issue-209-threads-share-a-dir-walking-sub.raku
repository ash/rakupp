# Regression: issue #209 (2026-11-10).
#
# Two threads inside the same sub that walks /proc -- `.dir`, a regex on each
# name, a sub returning a hash per entry, `// next`, then `.grep` over the
# filled hash -- crashed the process (SIGSEGV, "corrupted size vs. prev_size").
# This is the same program over a directory made to look like /proc, so that
# it runs everywhere, with three threads and the main one walking it at once.
#
# Compiled with --exe its binary died with SIGSEGV in every run: the workers'
# matches and the main thread's wrote one `$/` (see
# threads-own-match-var-native.raku). t/run.raku builds it with --exe too and
# requires PASS of the binary.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

my $root = $*TMPDIR.add("rakupp-issue-209-$*PID");
$root.mkdir;
LEAVE { try { for $root.dir -> $d { .unlink for $d.dir; $d.rmdir }; $root.rmdir } }
for 1..120 -> $n {
    $root.add($n).mkdir;
    my $state = $n %% 11 ?? 'Z' !! 'S';
    $root.add("$n/stat").spurt: "$n (a name) $state {$n % 5} 0 0 0 -1 4194560 0 0 0 0 0 0 0 0 20 0 1 0 {$n * 3}\n";
}
$root.add('self').mkdir;

sub proc-stat(Int $pid --> Hash) {
    my $s = try $root.add("$pid/stat").slurp;
    return Nil without $s;
    my $close = $s.rindex(')');
    return Nil without $close;
    my @f = $s.substr($close + 2).words;
    return Nil if @f < 20;
    %( pid => $pid, state => @f[0], ppid => @f[1].Int, start => @f[19].Int );
}
sub descendants(Int $parent --> List) {
    my %parent;
    for $root.dir -> $d {
        my $n = $d.basename;
        next unless $n ~~ /^ \d+ $/;
        my %s = proc-stat($n.Int) // next;
        next if %s<state> eq 'Z';
        %parent{$n.Int} = %s<ppid>;
    }
    %parent.grep(*.value == $parent).map(*.key.Int).sort.List;
}

my $want = (1..120).grep({ $_ % 5 == 0 && !($_ %% 11) }).List;
check descendants(0), $want, 'one thread';
my $ended = False;
my @beats = (^3).map: -> $t {
    start { my $k = 0; my $ok = True; until $ended && $k > 0 { $ok &&= descendants(0) eqv $want; $k++ }; $ok }
};
my $mine = True;
for ^25 { $mine &&= descendants(0) eqv $want }
$ended = True;
check $mine, True, 'the main thread, while three others walk the same directory';
check (await @beats).List, (True, True, True), '...and each of the others';
check descendants(0), $want, 'once they are done';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
