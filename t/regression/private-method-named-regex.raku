# A PRIVATE method may be named `regex`, `rule` or `token`. The lexer read
# the word after `!` as a rule DECLARATOR — `method !regex() { … }` and
# `self!regex` both swallowed what followed (the declaration died "A
# unit-scoped sub definition is not allowed…", the call took the next method
# with it: "No such private method '!after'"). After `.` the word was already
# a name; after `!` it is too. Found by the sigil-free R&D.
#
# Runs under both engines: Rakudo passes every check natively.
#
# Contract: exit 0 + last line PASS.
my @fail;

class L {
    has $.log = '';
    method go() { self!regex; self!rule; self!token; self!after; $!log }
    method !regex() { $!log ~= 'x' }
    method !rule()  { $!log ~= 'r' }
    method !token   { $!log ~= 't' }
    method !after() { $!log ~= 'a' }
}
@fail.push('private regex/rule/token: ' ~ L.new.go.raku) unless L.new.go eq 'xrta';

# a real grammar still declares its rules
grammar G { token TOP { <a> <b> }; regex a { a }; rule b { b } }
@fail.push('grammar rules') unless G.parse('ab');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
