# A `$var` atom in a `regex`, `token` or `rule` value matches the variable's
# value, as Rakudo's does: `my $w = "b"; "ab" ~~ regex { a $w }` was Nil.
# Such a value runs its code blocks in a match scope under the scope it
# closed over, and its `$var` atoms were left for a match-time hook that
# nothing installed; they are spliced in that scope now, as an `rx//`'s are,
# so a Str matches as a literal, a Regex as a subpattern, and a variable a
# sub closed over is the one it reads. Code blocks keep their own `$vars`.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub m($s, $rx) { ($s ~~ $rx) ?? ~$/ !! Nil }

my $w = 'b';
ck m("ab", regex { a $w }), 'ab', 'a Str in a regex';
ck m("ab", token { a $w }), 'ab', 'in a token';
ck m("x a b", rule { a $w }), 'a b', 'in a rule';
my $q = regex { a $w }; ck m("ab", $q), 'ab', 'a regex held in a variable';
ck m("a.b", regex { a $w }), Nil, 'which wants its value';
my $dot = '.';
ck (m("a.b", regex { a $dot b }), m("axb", regex { a $dot b })), ('a.b', Nil), 'a metacharacter is literal';
my $r = rx/b+/; ck m("abb", regex { a $r }), 'abb', 'a Regex is a subpattern';
sub mk { my $v = 'b'; regex { a $v } }
ck m("ab", mk()), 'ab', 'a variable a sub closed over';
sub mk2($v) { token { a $v } }
ck (m("ab", mk2('b')), m("ac", mk2('c')), m("ac", mk2('b'))), ('ab', 'ac', Nil), "a sub's parameter";
my $s = 'x';
ck (m("ax", regex { a "$s" }), m('a$s', regex { a '$s' })), ('ax', 'a$s'), 'quoted';
my %h = k => 'b'; ck m("ab", regex { a $(%h<k>) }), 'ab', 'a $( … ) expression';
ck m("ab", regex { :my $x = 'b'; a $x }), 'ab', 'a :my variable';
ck m("aXa", regex { (a) X $0 }), 'aXa', 'a backreference stays one';
my $c = 0; m("ab", regex { a { $c++ } b }); ck $c, 1, 'a code block keeps its own';

say 'PASS';
