# Regression: `Backtrace.new` died with "No such method 'new' for invocant of
# type 'Backtrace'". The machinery was all there — Interpreter::captureBacktrace
# already built the innermost-first BacktraceFrame list that Exception.backtrace
# answers — but nothing let a program ASK for it without throwing something
# first. S29-context/evalfile.t does exactly that, and its last two assertions
# were unreachable without it.
#
# The same fix made a backtrace's fallback file `curDeclFile()` — the file whose
# top level is running — instead of always the main program, so frames from a
# module's or an EVALFILE'd file's mainline name that file.
# Contract: exit 0 + last line PASS.
my @fail;

# it answers a non-empty list of frames, and this program is in it
my $bt = Backtrace.new;
@fail.push('non-empty')  unless $bt.elems > 0;
@fail.push('names this file')
    unless $bt.list.grep(*.file.ends-with('backtrace-new.raku')).Bool;

# frames carry the trio Backtrace consumers read
@fail.push('frame .file/.line/.code')
    unless $bt.list[0].file.chars && $bt.list[0].line ~~ Int;

# taken from inside routines, the chain is at least as deep as at the mainline
sub inner { Backtrace.new }
sub outer { inner() }
@fail.push('deeper inside routines') unless outer().elems >= $bt.elems;

# the Int argument drops that many innermost frames (Rakudo's $offset), so a
# routine can report its caller's position rather than its own
@fail.push('offset drops frames') unless Backtrace.new(1).elems <= $bt.elems;
@fail.push('offset past the end is empty, not a crash')
    unless Backtrace.new(9999).elems == 0;

# a backtrace taken at a MODULE's top level names that module. (Rakudo's is
# the whole compiler chain and ours is one frame; what both agree on, and what
# consumers grep for, is that the module's own file is IN it — before this fix
# ours named the program that loaded the module and nothing else.)
my $dir = $*TMPDIR.add("rakupp-btnew-{$*PID}");
$dir.mkdir;
my $mod = $dir.add('BtProbe.rakumod');
$mod.spurt("unit module BtProbe;\nour \@WHERE = Backtrace.new.list.map(*.file);\n");
my $p = run($*EXECUTABLE.absolute, '-I', $dir.absolute, '-e',
            'use BtProbe; say @BtProbe::WHERE.grep(*.ends-with("BtProbe.rakumod")).elems',
            :out, :err);
my $got = $p.out.slurp(:close).chomp; $p.err.slurp(:close);
@fail.push("module mainline names the module, got '$got'") unless $got.Int > 0;
unlink($mod);
rmdir($dir);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
