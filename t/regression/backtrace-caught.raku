# Regression: a CAUGHT exception could not say where it came from (issue #67).
# `$!.backtrace` captured at the point of the .backtrace CALL — so it answered
# one frame, on the line of the question rather than the line of the throw —
# and `.Str` on the result dumped the frame hash instead of frame lines.
#
# The chain is now recorded in RakuError's constructor and handed to the
# exception object at the catch, so a caught exception carries the position it
# was thrown from, whoever catches it and whenever they ask.
# Contract: exit 0 + last line PASS.
my @fail;

sub inner  { die "thrown here" }        # line 12
sub outer  { inner() }                  # line 13
try { outer() }                         # line 14

my $bt = $!.backtrace;
@fail.push('a caught exception has frames') unless $bt.elems >= 3;

my @names = $bt.list.map(*.subname);
@fail.push("innermost frame is the thrower, got '{@names[0] // ''}'")
    unless (@names[0] // '') eq 'inner';
@fail.push("…then its caller, got '{@names[1] // ''}'")
    unless (@names[1] // '') eq 'outer';

# the LINES are where the code is, not where .backtrace was asked
@fail.push("throw line, got {$bt.list[0].line}") unless $bt.list[0].line == 12;
@fail.push("call line, got {$bt.list[1].line}")  unless $bt.list[1].line == 13;
@fail.push('every frame names this file')
    unless $bt.list.map(*.file).all.ends-with('backtrace-caught.raku');

# .Str is the Rakudo frame lines, not a hash dump
my $str = $bt.Str;
@fail.push("Backtrace.Str is frame lines, got:\n$str")
    unless $str ~~ /'in sub inner at ' .*? 'line 12'/;
@fail.push('…for every frame') unless $str ~~ /'in sub outer at ' .*? 'line 13'/;
@fail.push('a frame Str names its own routine')
    unless $bt.list[0].Str ~~ /'in sub inner at '/;

# .gist of the exception is message + frames (Rakudo), and .Str is the message
@fail.push('gist opens with the message') unless $!.gist.lines[0] eq 'thrown here';
@fail.push('gist carries the frames')     unless $!.gist ~~ /'in sub inner at '/;
@fail.push('Str is the message alone')    unless $!.Str eq 'thrown here';

# a rethrow keeps the ORIGINAL position — that is the point of recording it
sub relay { try { outer() }; $!.rethrow }
try { relay() }
@fail.push("rethrow keeps the origin line, got {$!.backtrace.list[0].line}")
    unless $!.backtrace.list[0].line == 12;

# an exception thrown from a BUILTIN (C++, no `die` in sight) carries a chain too
sub boom { my $x = 42; $x.nonexistent-method }
try { boom() }
@fail.push('a builtin throw records its frames')
    unless $!.backtrace.list.map(*.subname).grep('boom');

# asking twice answers the same frames (the list is built once and cached)
try { outer() }
my $a = $!.backtrace.list.map({ .subname ~ '@' ~ .line }).join(',');
my $b = $!.backtrace.list.map({ .subname ~ '@' ~ .line }).join(',');
@fail.push("stable across asks: '$a' vs '$b'") unless $a eq $b;

# Backtrace.new still works and still names this file (t/regression/backtrace-new.raku)
@fail.push('Backtrace.new is unbroken') unless Backtrace.new.elems > 0;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
