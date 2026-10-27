# What issue #202's fix left open, against Rakudo.
#
# - A module's EXPORT held only its routines, every one under DEFAULT: an
#   exported variable, constant, class, role, enum (and its members) or
#   subset was in no tag package, and `is export(:tg)` was listed under
#   DEFAULT. Each symbol is now in each of its tags (DEFAULT when it names
#   none) and ALL, the tag packages are listed in EXPORT, and the variable's
#   entry is its container.
# - A package declared in the program, a class, and a package enclosing one
#   that exports (`module Outer { module Inner { sub i is export } }`) had
#   no EXPORT; Rakudo installs a symbol in the EXPORT of every package it is
#   declared within.
# - A package's Stash left out the subsets and modules declared in it.
# - `Foo::<$x>:p` was the value and a separate Pair `p => True`; `:k`, `:v`,
#   `:kv`, `:!p`, a slice and `:delete` likewise. They subscript the Stash now.
# - Imports ignored the tags of everything but a routine: a plain `use`
#   brought in `our $tv is export(:vt)` and `constant TK is export(:ct)`,
#   `use M :vt` brought in M's DEFAULT variables, constants and enum
#   members, `need M` its constants, and a constant or enum a module
#   declared WITHOUT exporting it reached its importer anyway.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

my $dir = $*TMPDIR.add("export-tags-{$*PID}");
$dir.mkdir;
$dir.add('XTAll.rakumod').spurt: q:to/END/;
    unit module XTAll;
    our $v is export = 1;
    our @arr is export = 1, 2;
    constant K is export = 3;
    class Cls is export { }
    role Rl is export { }
    enum Col is export <Red Green>;
    subset Pos of Int is export where * > 0;
    sub plain() is export { 1 }
    multi mm(Int) is export { 1 }
    multi mm(Str) is export { 2 }
    sub tagged() is export(:tg) { 1 }
    sub both() is export(:DEFAULT, :tg2) { 1 }
    sub mand() is export(:MANDATORY) { 1 }
    our $tv is export(:vt) = 5;
    constant TK is export(:ct) = 6;
    enum TE is export(:et) <tea teb>;
    sub notexp() { 1 }
    module Sub1 { our sub s1() is export { 1 } }
    END
$dir.add('XTHidden.rakumod').spurt: q:to/END/;
    unit module XTHidden;
    constant HX = 5;
    enum HE <ha hb>;
    our sub get-hx() is export { HX }
    END

sub run-out(Str $code) {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err);
    my $out = $p.out.slurp(:close).trim;
    my $err = $p.err.slurp(:close);
    $p.exitcode == 0 ?? $out !! 'DIES'
}

# the EXPORT packages, tag by tag
check run-out(q:to/END/).lines.List,
    need XTAll;
    say XTAll::.keys.sort.join(' ');
    say XTAll::EXPORT::.keys.sort.join(' ');
    for XTAll::EXPORT::.keys.sort -> $t { say "$t: ", XTAll::EXPORT::{$t}.WHO.keys.sort.join(' ') }
    say XTAll::EXPORT::DEFAULT::<$v> =:= $XTAll::v;
    $XTAll::v = 10; say XTAll::EXPORT::ALL::<$v>;
    say XTAll::EXPORT::DEFAULT::<Cls>.^name, ' ', XTAll::EXPORT::DEFAULT::<Red>, ' ', XTAll::EXPORT::DEFAULT::<Pos>.^name;
    say XTAll::Sub1::.keys.sort.join(' ');
    END
    ('$tv $v @arr Cls Col EXPORT Green K Pos Red Rl Sub1 TE TK tea teb',
     'ALL DEFAULT MANDATORY ct et tg tg2 vt',
     'ALL: $tv $v &both &mand &mm &plain &s1 &tagged @arr Cls Col Green K Pos Red Rl TE TK tea teb',
     'DEFAULT: $v &both &mm &plain &s1 @arr Cls Col Green K Pos Red Rl',
     'MANDATORY: &mand',
     'ct: TK',
     'et: TE tea teb',
     'tg: &tagged',
     'tg2: &both',
     'vt: $tv',
     'True',
     '10',
     'XTAll::Cls Red XTAll::Pos',
     '&s1 EXPORT'),
    'a unit module\'s EXPORT, every kind under its tags';

# packages declared in the program, and the packages around them
module XM { our $x = 1; our sub f() is export { 1 }; class C is export { }; our $e is export = 2; constant KK is export = 4 }
module XN { our $y = 1 }
class XQ { our sub h() is export { 1 }; method m { } }
module XOuter { module XInner { our sub i() is export { 1 } } }
check XM::.keys.sort.List, <$e $x &f C EXPORT KK>, 'a program\'s package lists its EXPORT';
check XM::EXPORT::DEFAULT::.keys.sort.List, <$e &f C KK>, '…which holds every kind';
check XN::.keys.sort.List, ('$y',), 'a package that exports nothing has none';
check XQ::.keys.sort.List, <&h EXPORT>, 'a class is a package with an EXPORT';
check XOuter::.keys.sort.List, <EXPORT XInner>, 'the package around an exporting one has an EXPORT';
check XOuter::EXPORT::ALL::.keys.sort.List, ('&i',), '…holding what the inner one exported';

# `Foo::<…>` takes every adverb, and a slice
check (XM::<$x>:p), ('$x' => 1), ':p';
check (XM::<$x>:k), '$x', ':k';
check (XM::<$x>:v), 1, ':v';
check (XM::<$x>:kv).List, ('$x', 1), ':kv';
check (XM::<$nope>:p).List, (), ':p of a missing key';
check (XM::<$x>:!p), ('$x' => 1), ':!p';
check (XM::<$x $e>:p).List, ('$x' => 1, '$e' => 2), 'a slice';
check (XM::<$x>:delete), 1, ':delete answers the value…';
check XM::.keys.sort.List, <$e &f C EXPORT KK>, '…and the symbol is gone';
check $XM::x, Any, '…from its long name too';

# imports by tag
my @cases =
    'use XTAll; say $v',             '1',
    'use XTAll; say K',              '3',
    'use XTAll; say Red',            'Red',
    'use XTAll; say Pos.^name',      'XTAll::Pos',
    'use XTAll; say mand()',         '1',
    'use XTAll; say $tv',            'DIES',
    'use XTAll; say TK',             'DIES',
    'use XTAll; say tea',            'DIES',
    'use XTAll; say tagged()',       'DIES',
    'use XTAll :vt; say $tv',        '5',
    'use XTAll :vt; say $v',         'DIES',
    'use XTAll :vt; say K',          'DIES',
    'use XTAll :vt; say Red',        'DIES',
    'use XTAll :vt; say mand()',     '1',
    'use XTAll :ct; say TK',         '6',
    'use XTAll :et; say teb',        'teb',
    'use XTAll :ALL; say $tv, TK, K, $v, Red, tea', '5631Redtea',
    'use XTAll :DEFAULT; say K',     '3',
    'need XTAll; say K',             'DIES',
    'need XTAll; say Red',           'DIES',
    'use XTHidden; say get-hx()',    '5',
    'use XTHidden; say HX',          'DIES',
    'use XTHidden; say ha',          'DIES',
    'use XTAll; { use XTAll :vt; print $tv }; say ""', '5';
for @cases -> $code, $want {
    check run-out($code), $want, $code;
}

for $dir.dir { .d ?? run('rm', '-rf', ~$_) !! .unlink }
$dir.rmdir;

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
