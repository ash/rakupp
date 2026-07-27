# Regression: Whatever arguments, the 6.e `rotor` sub, `--> T` on the anonymous
# routine forms, `$:name` in a string, and Pair.Hash.
#   * `.splice(*-2, *-1)` — `.toInt()` on a WhateverCode is 0, so both arguments
#     collapsed to 0 and the splice removed nothing from the front. They resolve
#     against the length the way .head/.tail already did, and the COUNT resolves
#     against what is left after the start.
#   * `rotor(CYCLE…, LIST)` — the 6.e SUB form takes the cycle first and the
#     iterable last, the opposite way round from the method. Every positional was
#     being swept into the list, so the cycle got rotored too. `:partial` may come
#     last, so the iterable is the last POSITIONAL, not the last argument.
#   * a `--> T` in the signature of a pointy block or an anonymous `sub`/`method`
#     was parsed and dropped — only a NAMED sub kept it.
#   * `$:name` is the implicit NAMED placeholder parameter and interpolates like
#     any other twigilled variable; the string scanner knew `* ! . ^` but not `:`.
#   * `.Hash`/`.Map` on a Pair is the one-entry hash it describes.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# splice resolves Whatever
my @f = <a b c d e f g>;
check(@f.splice(*-2, *-1).gist, '[f]',           'splice(*-2, *-1) removes one from the end');
check(@f.gist,                  '[a b c d e g]', 'and leaves the rest');
my @j = <a b c d>;
check(@j.splice(*-2).gist, '[c d]', 'splice(*-2) takes the tail');
check(@j.gist,             '[a b]', 'leaving the head');
my @g = 1..5;
check(@g.splice(1, 2).gist, '[2 3]',   'ordinary integer arguments still work');
check(@g.gist,              '[1 4 5]', 'and remove the right window');
my @i = 1..5; @i.splice(1, 2, 'x', 'y', 'z');
check(@i.gist, '[1 x y z 4 5]', 'a replacement splices in');

# the rotor METHOD (the 6.e SUB form is checked in rotor-sub-6e.raku, which
# needs the pragma at file scope)
check((1..7).rotor(2).join('|'),         '1 2|3 4|5 6',       'the METHOD is unchanged');
check((1..6).rotor(2 => 1).join('|'),    '1 2|4 5',           'and still takes a Pair cycle');

# --> T on the anonymous forms
check((-> $x --> Int { $x }).of.gist, '(Int)', 'a pointy block keeps its return type');
check((sub (--> Int) {}).of.gist,     '(Int)', 'and an anonymous sub');
check((-> $x { $x }).of.gist,         '(Mu)',  'without one it is Mu');
check((-> $x --> Int { $x }).signature.gist, '($x --> Int)', 'and it renders in the signature');

# $:name interpolates
sub greet { "got $:foo" }
check(greet(foo => 42), 'got 42', 'the named placeholder interpolates');
sub add2 { "$:a+$:b" }
check(add2(a => 1, b => 2), '1+2', 'two of them');
my $plain = 5;
check("plain $plain", 'plain 5', 'an ordinary variable is unaffected');

# Pair.Hash
check((a => 1).Hash.raku,      '{:a(1)}',       'a Pair as a one-entry Hash');
check((a => [1, 2]).Hash.raku, '{:a($[1, 2])}', 'with a list value');
check((a => 1).Map.raku,       'Map.new((:a(1)))', 'and as a Map');
check((a => 1).List.gist,      '(a => 1)',      '.List is still the pair itself');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
