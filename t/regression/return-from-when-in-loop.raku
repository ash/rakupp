# Regression: `return` from inside a `when` inside a `for` left the when-match
# flag (givenCtl) set. The loop saw the cooperative return and left; the flag
# rode out of the routine and cut the CALLER's next block short after one
# statement. Cro's run-body-handler has exactly this shape, and the later
# `with Grammar.parse(…) { .ast }` in Cro::MediaType.parse yielded Any — every
# `request-body` route then died serialising its response.
# Contract: exit 0 + last line PASS.
my @fail;

sub early() { for 1 { when 1 { return 5 } } }
sub early-catch() { for 1 { when 1 { CATCH { default { } }; return 6 } } }

class C { method m(Str() $s) { with $s.uc { .lc ~ "!" } else { "none" } } }

@fail.push("return value") unless early() == 5;
@fail.push("with after early: {C.m('AbC').raku}") unless C.m('AbC') eq 'abc!';
early-catch();
@fail.push("with after early-catch") unless C.m('XY') eq 'xy!';

# the caller's own blocks run to the end
my @log;
for 7 { early(); @log.push("for:$_") }
given 'G' { early(); @log.push("given:$_") }
if True { early(); @log.push('if') }
@fail.push("caller blocks: @log[]") unless @log eqv ['for:7', 'given:G', 'if'];

# a when that matches without returning still ends its loop iteration
my @it;
for 1..3 { when 2 { @it.push('two') }; @it.push($_) }
@fail.push("when-next: @it[]") unless @it eqv [1, 'two', 3];

# and a returning when inside given returns from the routine
sub g($x) { given $x { when 1 { return 'one' }; default { 'other' } } }
@fail.push("given-return") unless g(1) eq 'one' && g(2) eq 'other';

# a CATCH's `default { return … }` still handles what it caught: the handler
# reads the when-match flag (clearing it at `return` broke every such helper)
sub caught() { die 'x'; CATCH { default { return 'handled' } } }
@fail.push("catch-default-return") unless caught() eq 'handled';
sub caught-in-loop() { for 1 { die 'y'; CATCH { default { return 'in-loop' } } }; 'fell through' }
@fail.push("catch-in-loop: {caught-in-loop()}") unless caught-in-loop() eq 'in-loop';
sub caught-when() { die X::AdHoc.new(payload => 'z'); CATCH { when X::AdHoc { return 'typed' } } }
@fail.push("catch-when-return") unless caught-when() eq 'typed';

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
