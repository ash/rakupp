# rakudo#2794 — a MAIN argument that NAMES an enum value IS that value.
#
# The generalization of issue #95, whose fix converted Bool's four spellings by
# hand. The real rule is one lookup: the command line resolves each argument
# against the PROGRAM's own scope and takes what it finds when that is an enum
# value, so `enum Color <Red …>` makes `prog Red` arrive as `Color::Red` — and
# `True` was only ever the Bool enum's member reached the same way. Everything
# the lookup does not find, or finds bound to something that is not an enum
# value, goes through val() as before.
#
# What it must NOT convert is the point of half this matrix, since the rule
# reinterprets every argument of every program: a type name (`Int`, `Any`), an
# enum's own name (`Color`), a class, a sub, a constant holding a non-enum, and
# any name the program has taken for something else — `my constant True =
# "shadow"` leaves `True` the string, so a program that redefines a name keeps
# its own meaning. A constant BOUND to an enum value does convert; what is
# looked at is the value, not how the name was declared.
#
# Every expectation here IS Rakudo's output (2026.06), case by case.
#
# Known differences from Rakudo, deliberate, and left unpinned on purpose —
# each is an exotic corner whose expectation would be ours rather than the
# oracle's, and this file is only worth what its agreement with Rakudo is:
#   * `my enum Priv <Alpha>` — Rakudo converts the bare `Alpha` but NOT the
#     qualified `Priv::Alpha`, whose package is in no runtime stash there. Here
#     a lexical enum binds both spellings, so both convert. Not pinned.
#   * --target=js registers the enum MEMBERS it compiles and CORE's Bool, Order
#     and PromiseStatus; a constant merely HOLDING an enum value is not a member
#     and is not registered, and the enums that runtime has no support for
#     (Endian, Signal, SeekType, ProtocolType) are not converted there.
#   * an ANONYMOUS `enum <Aa Bb>` has its members converted here as Rakudo
#     converts them, but they still RENDER differently (`.^name` is `Int` and
#     `.raku` is `"Aa"`, against Rakudo's `` and `::Aa`) — a gap that predates
#     this and shows just the same for `say Aa` in ordinary code. Not pinned.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eq $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}

# run a program with args; 'USAGE' when dispatch failed, else the first stdout line
sub first-line(Str $prog, *@args) {
    my $p = run($*EXECUTABLE.absolute, '-e', $prog, |@args, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $p.exitcode == 2 ?? 'USAGE' !! ($out.lines[0] // '')
}

# --- a program's own enum ------------------------------------------------
my $color = 'enum Color <Red Green Blue>; sub MAIN($a) { say $a.^name ~ " " ~ $a.raku }';
check('a member arrives as the enum value',   first-line($color, 'Red'),   'Color Color::Red');
check('…each of them',                        first-line($color, 'Blue'),  'Color Color::Blue');
check('the qualified spelling too',           first-line($color, 'Color::Red'), 'Color Color::Red');
check('the enum TYPE is not a member',        first-line($color, 'Color'), 'Str "Color"');
check('an unrelated word is untouched',       first-line($color, 'Reddish'), 'Str "Reddish"');
check('and a number still allomorphs',        first-line($color, '42'),    'IntStr IntStr.new(42, "42")');

# the option form reads by the very same rule
my $opt = 'enum Color <Red Green>; sub MAIN(:$c) { say $c.^name ~ " " ~ $c.raku }';
check('--c=Red converts as a positional does', first-line($opt, '--c=Red'), 'Color Color::Red');
check('--c=Color::Green too',                  first-line($opt, '--c=Color::Green'), 'Color Color::Green');
check('--c=nope stays a string',               first-line($opt, '--c=nope'), 'Str "nope"');

# which is what lets an enum-TYPED parameter be driven from the command line
my $typed = 'enum Color <Red Green>; sub MAIN(Color $c) { say "got " ~ $c.raku }';
check('an enum-typed parameter binds',        first-line($typed, 'Green'), 'got Color::Green');
check('…and refuses a word that is not one',  first-line($typed, 'Mauve'), 'USAGE');

# --- CORE's own enums ----------------------------------------------------
my $any = 'sub MAIN($a) { say $a.^name ~ " " ~ $a.raku }';
check('True is the Bool member',       first-line($any, 'True'),      'Bool Bool::True');
check('False as well',                 first-line($any, 'False'),     'Bool Bool::False');
check('Order::Less, bare',             first-line($any, 'Less'),      'Order Order::Less');
check('…and qualified',                first-line($any, 'Order::More'), 'Order Order::More');
check('Same, the third one',           first-line($any, 'Same'),      'Order Order::Same');
check('PromiseStatus::Kept',           first-line($any, 'Kept'),      'PromiseStatus PromiseStatus::Kept');
check('PromiseStatus::Planned',        first-line($any, 'Planned'),   'PromiseStatus PromiseStatus::Planned');
check('PromiseStatus::Broken',         first-line($any, 'Broken'),    'PromiseStatus PromiseStatus::Broken');
check('Endian::BigEndian',             first-line($any, 'BigEndian'), 'Endian Endian::BigEndian');
check('Endian::NativeEndian',          first-line($any, 'NativeEndian'), 'Endian Endian::NativeEndian');
check('SeekType::SeekFromEnd',         first-line($any, 'SeekFromEnd'), 'SeekType SeekType::SeekFromEnd');
check('ProtocolType::PROTO_TCP',       first-line($any, 'PROTO_TCP'), 'ProtocolType ProtocolType::PROTO_TCP');
check('a Signal member',               first-line($any, 'SIGINT'),    'Signal Signal::SIGINT');
check('…and one that names no signal', first-line($any, 'SIGNOPE'),   'Str "SIGNOPE"');

# the ordinals are Rakudo's: Planned 0, Kept 1, Broken 2
my $ord = 'sub MAIN($a) { say $a.Int }';
check('Planned is 0',  first-line($ord, 'Planned'), '0');
check('Kept is 1',     first-line($ord, 'Kept'),    '1');
check('Broken is 2',   first-line($ord, 'Broken'),  '2');

# --- what the rule must leave alone --------------------------------------
check('a type name is no enum value',   first-line($any, 'Int'),  'Str "Int"');
check('…nor Any',                       first-line($any, 'Any'),  'Str "Any"');
check('…nor Mu',                        first-line($any, 'Mu'),   'Str "Mu"');
check('…nor Nil',                       first-line($any, 'Nil'),  'Str "Nil"');
check('…nor the enum type Bool itself', first-line($any, 'Bool'), 'Str "Bool"');

my $decls = q:to/PROG/;
    constant CENUM = Order::Less;
    constant CINT  = 42;
    constant CSTR  = "hi";
    constant CBOOL = True;
    class Klass {}
    sub helper() { 7 }
    sub MAIN($a) { say $a.^name ~ " " ~ $a.raku }
    PROG
check('a constant HOLDING an enum value converts', first-line($decls, 'CENUM'), 'Order Order::Less');
check('…and one holding a Bool',                   first-line($decls, 'CBOOL'), 'Bool Bool::True');
check('a constant holding an Int does not',        first-line($decls, 'CINT'),  'Str "CINT"');
check('nor one holding a Str',                     first-line($decls, 'CSTR'),  'Str "CSTR"');
check('a class name does not',                     first-line($decls, 'Klass'), 'Str "Klass"');
check('a sub name does not — and is NOT called',   first-line($decls, 'helper'), 'Str "helper"');

# a name the program has taken keeps the program's meaning, whatever CORE says
my $shadow = 'my constant True = "shadow"; sub MAIN($a) { say $a.^name ~ " " ~ $a.raku }';
check('a shadowed True stays the word',    first-line($shadow, 'True'), 'Str "True"');
check('…while the qualified name still converts', first-line($shadow, 'Bool::True'), 'Bool Bool::True');

# an enum whose members shadow CORE's (roast S06-other/main.t leans on this)
my $over = 'enum Mine <Less True>; sub MAIN($a) { say $a.^name ~ " " ~ $a.raku }';
check("a program's Less beats CORE's",  first-line($over, 'Less'), 'Mine Mine::Less');
check("…and its True beats Bool's",     first-line($over, 'True'), 'Mine Mine::True');

# --- position in the command line does not change the reading ------------
my $slurp = 'enum Color <Red>; sub MAIN(*@a) { say @a.raku }';
check('after a bare --, still converted',
      first-line($slurp, '--', 'Red', 'True'), '[Color::Red, Bool::True]');
check('in the tail past the first positional, too',
      first-line($slurp, 'pos', 'Red'), '["pos", Color::Red]');

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
