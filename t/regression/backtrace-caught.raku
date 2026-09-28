# Regression: a CAUGHT exception could not say where it came from (issue #67).
# `$!.backtrace` captured at the point of the .backtrace CALL — so it answered
# one frame, on the line of the question rather than the line of the throw —
# and `.Str` on the result dumped the frame hash instead of frame lines.
#
# The chain is now recorded in RakuError's constructor and handed to the
# exception object at the catch, so a caught exception carries the position it
# was thrown from, whoever catches it and whenever they ask.
#
# The list starts, as Rakudo's does, with the setting's own `throw` and `die`
# (is-setting True); the code's frames follow, so the thrower is the first
# frame that is NOT the setting's.
# Contract: exit 0 + last line PASS.
my @fail;

sub inner  { die "thrown here" }        # line 16
sub outer  { inner() }                  # line 17
try { outer() }                         # line 18

my $bt = $!.backtrace;
@fail.push('a caught exception has frames') unless $bt.elems >= 3;
my @own = $bt.list.grep(!*.is-setting);    # the program's frames, innermost first
@fail.push('the setting frames lead, is-setting') unless $bt.list[0].is-setting;

my @names = @own.map(*.subname);
@fail.push("innermost frame is the thrower, got '{@names[0] // ''}'")
    unless (@names[0] // '') eq 'inner';
@fail.push("…then its caller, got '{@names[1] // ''}'")
    unless (@names[1] // '') eq 'outer';

# the LINES are where the code is, not where .backtrace was asked
@fail.push("throw line, got {@own[0].line}") unless @own[0].line == 16;
@fail.push("call line, got {@own[1].line}")  unless @own[1].line == 17;
@fail.push('every frame of the program names this file')
    unless @own.map(*.file).all.ends-with('backtrace-caught.raku');

# .Str is the Rakudo frame lines, not a hash dump
my $str = $bt.Str;
@fail.push("Backtrace.Str is frame lines, got:\n$str")
    unless $str ~~ /'in sub inner at ' .*? 'line 16'/;
@fail.push('…for every frame') unless $str ~~ /'in sub outer at ' .*? 'line 17'/;
@fail.push('a frame Str names its own routine')
    unless @own[0].Str ~~ /'in sub inner at '/;

# .gist of the exception is message + frames (Rakudo), and .Str is the message
@fail.push('gist opens with the message') unless $!.gist.lines[0] eq 'thrown here';
@fail.push('gist carries the frames')     unless $!.gist ~~ /'in sub inner at '/;
@fail.push('Str is the message alone')    unless $!.Str eq 'thrown here';

# a rethrow keeps the ORIGINAL position — that is the point of recording it
sub relay { try { outer() }; $!.rethrow }
try { relay() }
@fail.push("rethrow keeps the origin line, got {$!.backtrace.list.first(!*.is-setting).line}")
    unless $!.backtrace.list.first(!*.is-setting).line == 16;

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
