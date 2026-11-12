# Regression (#256): `$0`, `$1`, … are `$/[0]`, `$/[1]`, … of the `$/` in
# scope — a `my $/`, a bound `$/`, a `$/` parameter, a pointy block's `$/`, an
# action method's `$/` — as `$/[0]` and `$<name>` already were.
#
# A match also wrote `$0..$N` as variables of their own in the scope it ran
# in, and a read found those first, so an inner `$/` read the captures of the
# last `~~` that ran anywhere visible. JSON::Tiny's actions guard on
# `$0.Str ne '"'` (its `string` token opens with `('"')`): after any
# capturing `~~` in the program the guard saw that capture, and every hash key
# gained the capture minus its first character (`{"a":1}` → `Za`). The grammar
# below is that shape.
#
# The slots also outlived the `$/` they came from: after a bare block's or a
# loop body's `~~`, after `$/ = Nil`, after `m:g` (whose `$/` is a List of
# Matches) and after a `.grep(/…/)`, which leaves `$/` alone.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $a = 'zzZZzz' ~~ /'zz' (.+?) 'zz'/;
my $b = '12ab' ~~ /(\d+)/;
{ my $/ = $a; check ~$0, 'ZZ', 'a block-local my $/'; }
{ my $/ := $a; check ~$0, 'ZZ', 'a bound $/'; }
sub f($/) { ~$0 }
check f($a), 'ZZ', 'a $/ parameter';
check (-> $/ { ~$0 })($a), 'ZZ', 'a pointy block $/';
check ~$0, '12', 'the mainline $/ is the last ~~';

grammar G { token TOP { (\d+) (\w+) } }
class C { method TOP($/) { make "$0|$1|$/[0]|$/[1]" } }
'12ab' ~~ /(\d+) (\w+)/;
check G.parse('77xy', :actions(C.new)).made, '77|xy|77|xy', 'an action method $/';
check "$0 $1", '77 xy', 'Grammar.parse sets the caller $/, and $0 with it';

grammar J {
    token TOP { '{' <pair>* % ',' '}' }
    token pair { <string> ':' \d+ }
    token string { ('"') ~ \" (<-["]>*) }
}
class JA {
    method TOP($/) { make $<pair>».made }
    method pair($/) { make $<string>.made }
    method string($/) { my $s = ~$1; $s = $0.Str.substr(1) ~ $s if $0.Str ne '"'; make $s }
}
'zzZZzz' ~~ /'zz' (.+?) 'zz'/;
check J.parse('{"a":1,"b":2}', :actions(JA)).made.join(','), 'a,b',
      'keys keep their text after a capturing ~~ (JSON::Tiny)';

sub r { 'cd' ~~ /(d)/; { 'ef' ~~ /(f)/ }; ~$0 }
check r(), 'f', 'a bare block shares the routine $/';
for <x1 y2> { $_ ~~ /(\d)/ }
check ~$0, '2', 'a loop body shares the mainline $/';
'pq' ~~ /(q)/; { $/ = Nil; check $0, Nil, '$/ = Nil leaves no $0'; }
'st' ~~ /(t)/; my @g = <a1 b2>.grep(/(\d)/);
check ~$0, 't', '.grep(/…/) leaves $/, and $0, alone';
my @m = 'a1b2' ~~ m:g/(\w)(\d)/;
check ($0.Str, $1.Str), ('a1', 'b2'), 'after m:g $0 is the first Match';
'xy' ~~ /(x)/; 'q' ~~ /z/;
check $0, Nil, 'a failed match leaves no $0';

# a code block sees the cursor, even where a $/ parameter stands, and the
# parameter is back after the match
sub q($/) { my $ok = so 'a5' ~~ / (\d) <?{ $0 > 3 }> /; ($ok, ~$0) }
check q('zz' ~~ /(z)/), (True, 'z'), 'a code block under a $/ parameter';
grammar H { token TOP { (\d) <?{ $0 > 3 }> (\w) { $*seen = "$0,$1" } } }
my $*seen = '';
check (?H.parse('7x'), $*seen), (True, '7,x'), 'a grammar code block';
check 'aXbYc'.subst(/(<[XY]>)/, { "[$0.lc()]" }, :g), 'a[x]b[y]c', 'a .subst closure';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
