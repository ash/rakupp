# Longest-token ranking sees through a call to another PROTO and through a
# call to a PARAMETERIZED rule, as Rakudo's NFA does (issue #286).
#
# Cro::WebApp templates died on `<!DOCTYPE html>` with "malformed condition
# tag": the grammar's `sequence-element` proto has a candidate that is just
# `<sigil-tag>` (another proto), and `literal-open-tag` reads `<tag-element>+`
# (a third). Both calls were model gaps, so the whole proto fell back to a
# probe that ran every candidate, and the condition tag's `|| <.malformed>`
# died before the literal tag was tried. Calls with arguments
# (`<sequence-element('iteration')>`, `<parameter>` with defaults) were gaps
# the same way.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# The shape of Cro's template grammar: the panic sits behind a code block, so
# only a candidate ranked FIRST can reach it.
grammar Tmpl {
    token TOP { <el>* }
    proto token el { * }
    token el:sym<sigil> { <sigil-tag> }
    token el:sym<text>  { <-[<]>+ }
    token el:sym<open>  { '<' <te>+ '>' }
    proto token sigil-tag { * }
    token sigil-tag:sym<cond> { '<' <[?!]> { } <cond> '>' }
    token cond { '.' \w+ || { die "malformed condition tag" } }
    proto token te { * }
    token te:sym<lit> { <-[<>]>+ }
}
for '<!DOCTYPE html>', "<html>\n<!DOCTYPE html>\n</html>", '<?xml version="1.0"?>' -> $t {
    my $ok = try so Tmpl.parse($t);
    check $ok, True, "nested protos rank by prefix: {$t.lines[0]}";
}
check so(Tmpl.parse('<?.ok>')), True, 'the condition tag still wins when it fits';

# A call with arguments ranks by the callee's body.
grammar P1 { token TOP { <el> .* }; proto token el {*}; token el:sym<b> { 'xy' }; token el:sym<a> { 'x' <p(1)> }; token p($n) { 'yz' } }
grammar P2 { token TOP { <el> .* }; proto token el {*}; token el:sym<b> { 'xy' }; token el:sym<a> { 'x' <p> }; token p(:$n = 1) { 'yz' } }
grammar P3 { token TOP { <el> .* }; proto token el {*}; token el:sym<b> { 'xy' }; token el:sym<a> { 'x' <q> }; proto token q($n = 1) {*}; token q:sym<z> { 'yz' } }
# …and a parameter the body reads ends the prefix there.
grammar P4 { token TOP { <el> .* }; proto token el {*}; token el:sym<b> { 'xy' }; token el:sym<a> { 'x' <p('y')> }; token p($n) { $n 'z' } }
check P1.parse('xyz')<el>.Str, 'xyz', 'positional argument';
check P2.parse('xyz')<el>.Str, 'xyz', 'named parameter with a default';
check P3.parse('xyz')<el>.Str, 'xyz', 'parameterized proto';
check P4.parse('xyz')<el>.Str, 'xy',  'an interpolated parameter ends the prefix';

# A nested candidate's <sym> is its own literal, not the caller's.
grammar S {
    token TOP { <el> .* }
    proto token el {*}
    token el:sym<ab>  { 'a' }
    token el:sym<inner> { <in> }
    proto token in {*}
    token in:sym<abc> { <sym> }
}
check S.parse('abc')<el>.Str, 'abc', 'nested <sym>';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
