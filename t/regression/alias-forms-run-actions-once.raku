# Regression (issue #108): every way of naming a subrule capture runs the
# subrule's action once and files ONE Match under each name. `$<lo>=<inner>`
# built a second Match without `.made` (and the subtree twice); `<a=b=inner>`
# looked for a rule called `b=inner` and failed the parse.
# Contract: exit 0 + last line PASS.
my %calls;
my class A { method inner($/) { %calls<inner>++; make 'M' ~ ~$/ }; method leaf($/) { %calls<leaf>++ } }
grammar G2 { token TOP { <lo=inner> };    token inner { <leaf> }; token leaf { \d+ } }
grammar G3 { token TOP { $<lo>=<inner> }; token inner { <leaf> }; token leaf { \d+ } }
grammar G4 { token TOP { <a=b=inner> };   token inner { <leaf> }; token leaf { \d+ } }
grammar G5 { token TOP { $<lo>=<inner>+ }; token inner { <leaf> }; token leaf { \d } }
my @fail;
for G2, 'lo', G3, 'lo', G4, 'a', G4, 'b', G4, 'inner' -> $g, $key {
    %calls = ();
    my $m = $g.parse('42', actions => A.new);
    @fail.push("{$g.^name} <$key>: no parse") and next unless $m;
    @fail.push("{$g.^name}: inner ran {%calls<inner> // 0}") unless %calls<inner> == 1;
    @fail.push("{$g.^name}: leaf ran {%calls<leaf> // 0}") unless %calls<leaf> == 1;
    @fail.push("{$g.^name} <$key>.made") unless ($m{$key} andthen .made) eq 'M42';
}
%calls = ();
my $m = G5.parse('42', actions => A.new);
@fail.push("G5 lo: {$m<lo>».made}") unless $m<lo>.map(*.made).List eqv ('M4', 'M2');
@fail.push("G5 inner ran {%calls<inner>}") unless %calls<inner> == 2;
grammar N { token TOP { <a=l1> }; token l1 { <b=l2> }; token l2 { <c=leaf> }; token leaf { \d+ } }
my $leaf = 0;
my class B { method leaf($/) { $leaf++ } }
N.parse('7', actions => B.new);
@fail.push("nested aliases: leaf ran $leaf") unless $leaf == 1;
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
