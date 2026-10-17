# Regression: declarator pod (`#|` above a declaration, `#=` beside it) reaches
# `.WHY` in a binary compiled with --exe. Every one of these answered Nil there:
# the generated code built subs, methods, classes, attributes and enums without
# the pod the parser had attached. Two more on the way, in the interpreter too:
# a MODULE file's declarator pod was parsed away by the module loader, and the
# AST cache dropped the `#=` part of routines and classes and all of an
# attribute's or an enum's pod. And `&f` of a module's exported sub was a closure
# forwarding to it by name in compiled code, not the routine itself.
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppDeclaratorPod;

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

#| a sub's lead
sub lead-only { }
ck ~&lead-only.WHY, 'a sub\'s lead', 'a sub with #|';

sub trail-only($x) {
    #= a trailer
}
ck ~&trail-only.WHY, 'a trailer', 'a sub with #=';

#| both: lead
sub both {
    #= both: trail
}
ck &both.WHY.leading, 'both: lead', '#| and #= — the leading part';
ck &both.WHY.trailing, 'both: trail', '#| and #= — the trailing part';

#| the group's proto
proto grouped(|) {*}
multi grouped(Int) { }
multi grouped(Str) { }
ck ~&grouped.WHY, 'the group\'s proto', 'a multi group is documented by its proto';

#| a class
class Doc {
    has $.x; #= an attribute
    #| a method
    method m { }
}
ck ~Doc.WHY, 'a class', 'a class';
ck ~Doc.^find_method('m').WHY, 'a method', 'a method';
ck ~Doc.^attributes[0].WHY, 'an attribute', 'an attribute';

#| an enum
enum Shade <Light Dark>;
ck ~Shade.WHY, 'an enum', 'an enum';

ck documented(), 'ran', 'the module sub still runs';
ck &documented.name, 'documented', '&f of a module sub is the routine itself';
ck &documented.WHY.leading, 'a module sub\'s lead', 'a module sub\'s #|';
ck &documented.WHY.trailing, '…and its trailer', 'a module sub\'s #=';
ck ~Documented.^attributes[0].WHY, 'a module attribute', 'a module class\'s attribute';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
