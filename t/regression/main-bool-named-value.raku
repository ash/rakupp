# Issue #95: `--tls=True` printed the usage message where Rakudo ran the program.
#
# A program that declares `Bool :$tls` is normally driven by the bare `--tls`
# flag, but a caller is entitled to spell the value out — the reporter's
# async-client.raku was invoked as `--show-metrics=True --tls=True` — and under
# Rakudo that works. The command line has a rule of its own for it, applied as
# every argument is read and BEFORE any signature is looked at: the words that
# name Bool's two values — `True`/`False`, bare or qualified — arrive as the
# Bool itself, everything else goes through val(). rakupp had only the val()
# half (issue #11), so `--tls=True` handed the Bool parameter the string
# "True", nothing bound, and usage was printed.
#
# The rule is about the spelling, not about the parameter, so the matrix below
# pins both directions: `--tls=1`, `--tls=yes` and `--tls=true` are NOT the Bool
# and must still fail to bind, while a `Str :$a` must REFUSE `--a=True` because
# what it is offered is a Bool. Positionals read the same way — `True` binds a
# `Bool $x` and does not bind a `Str $p`. Untyped parameters simply receive
# whichever of Bool/allomorph/Str the word produced.
#
# Rakudo arrives at those spellings by a wider rule — it looks the word up in
# the program's scope and takes whatever ENUM VALUE it finds, which is also why
# `enum Color <Red …>` makes `Red` arrive as `Color::Red` (rakudo#2794, roast
# S06-other/main.t "enums are converted"). Only Bool is converted here, so this
# file pins Bool's four spellings and leaves the rest as strings on purpose.
#
# Every expectation here IS Rakudo's output (2026.06), case by case.
# Fixed alongside the same rule in the --target=js runtime (src/js-rt/70-host.js),
# whose binder had been faking it with a truthy() coercion that turned even
# `--tls=False` into True.

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

# --- a Bool named: the reporter's case, and what is NOT it ----------------
my $bool = 'sub MAIN(Bool :$tls = False) { say "tls=" ~ $tls.raku }';
check('--tls=True binds the Bool',        first-line($bool, '--tls=True'),  'tls=Bool::True');
check('--tls=False binds it too',         first-line($bool, '--tls=False'), 'tls=Bool::False');
check('the bare flag is unchanged',       first-line($bool, '--tls'),       'tls=Bool::True');
check('--/tls still negates',             first-line($bool, '--/tls'),      'tls=Bool::False');
check('--tls=1 is not the Bool',          first-line($bool, '--tls=1'),     'USAGE');
check('--tls=0 is not the Bool',          first-line($bool, '--tls=0'),     'USAGE');
check('--tls=yes is not the Bool',        first-line($bool, '--tls=yes'),   'USAGE');
check('the rule is case-exact: --tls=true',  first-line($bool, '--tls=true'),  'USAGE');
check('the rule is case-exact: --tls=false', first-line($bool, '--tls=false'), 'USAGE');
check('an empty --tls= is not the Bool',  first-line($bool, '--tls='),      'USAGE');
check('--tls=Nil is not the Bool',        first-line($bool, '--tls=Nil'),   'USAGE');

# the qualified spelling of the same two values, which Rakudo reaches by the
# same lookup — `Bool::Nope` names nothing and stays a string
check('--tls=Bool::True binds it',        first-line($bool, '--tls=Bool::True'),  'tls=Bool::True');
check('--tls=Bool::False binds it',       first-line($bool, '--tls=Bool::False'), 'tls=Bool::False');
check('--tls=Bool::Nope does not',        first-line($bool, '--tls=Bool::Nope'),  'USAGE');
check('…nor the miscased bool::True',     first-line($bool, '--tls=bool::True'),  'USAGE');

# the reporter's own signature shape: two spelled-out Bools beside other types
my $client = q:to/PROG/;
    sub MAIN(Str :$host = "127.0.0.1", Int :$num-clients = 3,
             Bool :$tls = False, Bool :$show-metrics = False) {
        say "host=$host n=$num-clients tls=$tls metrics=$show-metrics";
    }
    PROG
check('issue #95: the reported command line runs',
      first-line($client, '--num-clients=1', '--show-metrics=True', '--tls=True'),
      'host=127.0.0.1 n=1 tls=True metrics=True');

# --- the same words through the single-dash and colon spellings -----------
my $two = 'sub MAIN(Bool :$x = False, Bool :$y = True) { say "x=" ~ $x.raku ~ " y=" ~ $y.raku }';
check('-x=True binds through the single dash',  first-line($two, '-x=True'),  'x=Bool::True y=Bool::True');
check(':y=False binds through the colon form',  first-line($two, ':y=False'), 'x=Bool::False y=Bool::False');

# a repeated option still collects into a list, so it binds :@x and not a scalar
my $rep = 'sub MAIN(:@x) { say "x=" ~ @x.raku }';
check('a repeated --x=True/--x=False collects both Bools',
      first-line($rep, '--x=True', '--x=False'), 'x=[Bool::True, Bool::False]');

# --- the other direction: a Str named is handed a Bool and refuses it -----
my $str = 'sub MAIN(Str :$a = "d") { say "a=" ~ $a.raku }';
check('a Str named REFUSES --a=True',     first-line($str, '--a=True'),  'USAGE');
check('a Str named REFUSES --a=False',    first-line($str, '--a=False'), 'USAGE');
check('…but takes any other word',        first-line($str, '--a=true'),  'a="true"');

# --- an untyped named takes whatever the word produced -------------------
my $any = 'sub MAIN(:$b) { say "b=" ~ $b.raku }';
check('untyped named: True is a Bool',    first-line($any, '--b=True'),  'b=Bool::True');
check('untyped named: False is a Bool',   first-line($any, '--b=False'), 'b=Bool::False');
check('untyped named: a number still allomorphs', first-line($any, '--b=42'),
      'b=IntStr.new(42, "42")');
check('untyped named: a word is still a Str',     first-line($any, '--b=hi'), 'b="hi"');

# --- positionals read by the identical rule ------------------------------
my $pos = 'sub MAIN($x) { say "x=" ~ $x.raku }';
check('a positional True is a Bool',      first-line($pos, 'True'),  'x=Bool::True');
check('a positional False is a Bool',     first-line($pos, 'False'), 'x=Bool::False');
check('a positional number still allomorphs', first-line($pos, '3.5'), 'x=RatStr.new(3.5, "3.5")');

my $posb = 'sub MAIN(Bool $x) { say "x=" ~ $x.raku }';
check('a positional Bool takes True',     first-line($posb, 'True'),  'x=Bool::True');
check('a positional Bool takes False',    first-line($posb, 'False'), 'x=Bool::False');
check('a positional Bool refuses yes',    first-line($posb, 'yes'),   'USAGE');
check('a positional Bool refuses 1',      first-line($posb, '1'),     'USAGE');
check('a positional takes the qualified spelling too',
      first-line($posb, 'Bool::False'), 'x=Bool::False');

my $poss = 'sub MAIN(Str $p) { say "p=" ~ $p.raku }';
check('a positional Str REFUSES True',    first-line($poss, 'True'), 'USAGE');
check('…but takes any other word',        first-line($poss, 'hi'),   'p="hi"');

# --- where this rule meets the space-form pairing one --------------------
# `--foo abc` pairs into :foo<abc> for a Str named param, and the paired value
# is read like any other: `--foo True` therefore hands that Str param a Bool
# and fails, where `--foo --v` still yields the literal string.
my $pair = 'sub MAIN(Str :$foo = "d", Bool :$v = False) { say "foo=" ~ $foo.raku ~ " v=" ~ $v.raku }';
check('a paired value still pairs',       first-line($pair, '--foo', 'abc'), 'foo="abc" v=Bool::False');
check('…and a paired True does not bind the Str', first-line($pair, '--foo', 'True'), 'USAGE');
check('…while a paired option name stays a string',
      first-line($pair, '--foo', '--v'), 'foo="--v" v=Bool::False');

# --- the conversion reaches the tail the boundary rules drain verbatim ---
# `--` and the first-positional boundary stop OPTION parsing, not the reading
# of each word: a True past either of them is still the Bool.
my $slurp = 'sub MAIN(*@a) { say "a=" ~ @a.raku }';
check('after a bare --, True is still a Bool',
      first-line($slurp, '--', 'True', 'False'), 'a=[Bool::True, Bool::False]');
check('in the tail past the first positional, too',
      first-line($slurp, 'pos', 'True'), 'a=["pos", Bool::True]');

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
