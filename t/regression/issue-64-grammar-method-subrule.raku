# Issue #64: `<.method>` never called an ordinary method of the grammar.
#
# Rakudo compiles every subrule call as a method call on the cursor, so a
# grammar's `[ 'c' || <.panic("expected c")> ]` reaches `method panic` — the
# standard way a grammar reports a parse error with a line number. rakupp
# resolved `<name>` against its rule table and the built-in classes only; an
# ordinary method was an unknown subrule, which failed silently, so `parse`
# answered Nil and the `die` inside the method never ran.
#
# A name that is neither rule, proto nor built-in but IS a method of the
# grammar (or of a parent, or a composed role) now invokes it. Its `self` is
# a cursor: a Match at the call position carrying the whole subject (.pos,
# .target, .orig), which answers the grammar's rules as methods (`self.b`
# matches in the running parse) and its other methods. A returned Match
# continues the parse at its end; `self` is a zero-width pass; a failed rule
# call fails the subrule; a die leaves the parse as the caller's exception.
#
# Contract: exit 0 + last line PASS. Every expectation here is Rakudo's.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# the reproducer from the issue: the method runs, its die is the caller's
my $called = False;
grammar Probe {
    token TOP { 'a' <.probe> }
    method probe { $called = True; die "boom" }
}
my $m = try Probe.parse("a");
check $called, True, 'the method body ran';
check ($! ?? $!.message !! 'no exception'), 'boom', 'its die came out of parse';
check $m.defined, False, 'and the parse has no result';

# after a parse died mid-way, the surrounding scope is intact
my $after = 42;
check $after, 42, 'a variable declared after the failed parse is readable';
check Probe.parse("b").defined, False, 'the same grammar still parses';

# the idiom itself: an error method with line/column context
grammar Lines {
    token TOP { <line>* [ $ || <.expected-line> ] }
    token line { 'ok' \n }
    method expected-line {
        my $line = self.target.substr(0, self.pos).lines.elems + 1;
        die "parse error at line $line";
    }
}
try Lines.parse("ok\nok\nbad\n");
check ($! ?? $!.message !! 'no exception'), 'parse error at line 3', 'self.target / self.pos locate the error';
check Lines.parse("ok\nok\n").defined, True, 'a clean input never reaches the method';

# arguments, .pos, .target, .orig on the cursor
grammar Args {
    token TOP { 'ab' [ 'c' || <.panic("expected c", 7)> ] }
    method panic($msg, $n) { die "$msg/$n at {self.pos} of '{self.target}' orig='{self.orig}'" }
}
try Args.parse("abd");
check ($! ?? $!.message !! 'no exception'), "expected c/7 at 2 of 'abd' orig='abd'", 'call arguments and the cursor accessors';

# a method that delegates to a rule: the parse continues after what it matched,
# and the capturing form records it under the method's name
grammar Delegate {
    token TOP { 'a' <probe> 'c' }
    token b { 'b' }
    method probe { self.b }
}
my $d = Delegate.parse("abc");
check $d.defined, True, 'self.b matched in the running parse';
check $d.to, 3, 'and the parse continued after it';
check ~$d<probe>, 'b', '<probe> captured what the rule matched';
check Delegate.parse("adc").defined, False, 'a failed rule call fails the subrule';

grammar DelegateDot {
    token TOP { 'a' <.probe> 'c' }
    token b { 'b' }
    method probe { self.b }
}
check DelegateDot.parse("abc").defined, True, '<.probe> — the non-capturing form';
check DelegateDot.subparse("abcd").to, 3, 'subparse stops where the parse stops';

# returning self is a zero-width pass
grammar Pass {
    token TOP { 'a' <.probe> 'b' }
    method probe { self }
}
check Pass.parse("ab").defined, True, 'a method returning self passes without consuming';

# the method may be inherited, or composed from a role
grammar Base { method panic($m) { die "base: $m" } }
grammar Child is Base { token TOP { 'a' [ 'b' || <.panic("no b")> ] } }
try Child.parse("ax");
check ($! ?? $!.message !! 'no exception'), 'base: no b', 'a parent grammar\'s method';

role Panics { method panic($m) { die "role: $m" } }
grammar Composed does Panics { token TOP { 'a' [ 'b' || <.panic("no b")> ] } }
try Composed.parse("ax");
check ($! ?? $!.message !! 'no exception'), 'role: no b', 'a method composed from a role';

# a method named like a built-in class wins over the built-in
grammar Shadow {
    token TOP { 'a' <.alpha> }
    method alpha { die "custom alpha" }
}
try Shadow.parse("ab");
check ($! ?? $!.message !! 'no exception'), 'custom alpha', 'method alpha shadows <alpha>';

# LTM ranking runs no user code: the longer branch wins and the panic in the
# shorter one is never reached
grammar Ranked {
    token TOP { [ <a> | <b> ] }
    token a { 'x' <.panic("a-branch")> }
    token b { 'xy' }
    method panic($m) { die $m }
}
my $ranked = try Ranked.parse("xy");
check ($ranked.defined ?? $ranked.to !! $!.message), 2, '`|` ranks past a branch that would panic';

# a cursor the method kept is a plain Match once the call is over
my $kept;
grammar Keep {
    token TOP { 'a' <.probe> }
    method probe { $kept = self; self }
}
check Keep.parse("a").defined, True, 'a method may keep its cursor';
check $kept.pos, 1, 'and read it afterwards';

# a grammar with no such method keeps the built-in class
grammar Plain { token TOP { 'a' <.alpha> } }
check Plain.parse("ab").defined, True, '<.alpha> is still the built-in where no method shadows it';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
