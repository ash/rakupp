# `run` and `shell` refuse a named argument they do not take, as Rakudo's
# dispatcher does: `run(…, :ENV(%env))` ran the child with this process's
# environment, and tools/run-fatroast.raku ran a whole campaign against the
# wrong directory that way. Everything each one takes still runs, and
# Proc::Async's `.start` stays lenient (in both engines). rakupp's own
# `run(…, :timeout)` stays too; this file does not use it, as it runs under
# Rakudo as well.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

try run('true', :bogus);
ck $!.^name, 'X::Multi::NoMatch', 'run(:bogus)';
ck $!.message.lines[0].ends-with(':bogus); none of these signatures matches:'), True, '…the message';
try shell('true', :ENV(%()));
ck $!.^name, 'X::Multi::NoMatch', 'shell(:ENV)';
ck $!.message.lines[0], 'Cannot resolve caller shell(Str:D, :ENV(Hash)); none of these signatures matches:',
   '…the message';
try run('true', :ENV(%()));
ck $!.^name, 'X::Multi::NoMatch', 'run(:ENV)';
try shell('true', :arg0<x>);
ck $!.^name, 'X::Multi::NoMatch', 'shell takes no :arg0';

my $p = run 'echo', 'hi', :out, :err, :!chomp, :merge, :enc<utf8>, :nl("\n"), :cwd($*CWD),
            :env(%*ENV), :arg0<x>, :!win-verbatim-args;
ck $p.out.slurp(:close), "hi\n", 'run takes all of its own';
my $s = shell 'echo hi', :out, :err, :!chomp, :merge, :enc<utf8>, :nl("\n"), :cwd($*CWD), :env(%*ENV);
ck $s.out.slurp(:close), "hi\n", 'shell takes all of its own';
my $done = await Proc::Async.new('true').start(:bogus);
ck $done.exitcode, 0, 'Proc::Async.start stays lenient';

say 'PASS';
