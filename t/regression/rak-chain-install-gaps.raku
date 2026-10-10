# Regression: the engine gaps that stood between lizmat's rak chain and a clean
# `rakupp install` (String::Utils, paths, highlighter, Needle::Compile, rak,
# App::Rak) once nqp::unipropcode, nqp::nextfiledir and the FIRST/state phaser
# work were done (see nqp-unipropcode-int.raku, nqp-nextfiledir-end-of-dir.raku,
# first-phaser-once-per-clone.raku, callable-for-phaser-sees-state.raku), and
# the two that kept rak itself from running a pattern's phasers:
#
#  1. nqp::istype(*, Whatever) was 0: String::Utils' `stem($basename, $parts = *)`
#     took the counted branch and answered "" for "foo.tar.gz".
#  2. `Cool:D $x` outranked a bare `@x` for a list: Rakudo weighs a smiley only
#     between two parameters of the same type, and Positional is narrower than
#     Cool. String::Utils' `paragraphs(Cool:D $string)` hands `$string.lines`
#     on to `paragraphs(@source)` and recursed into itself instead.
#  3. `$x but R(value)` / `but R<value>` set the role's attribute AFTER the
#     role's TWEAK ran: highlighter's `"bar" but Type<words>` checks `$!type`
#     in TWEAK and died.
#  4. `Empty` was not DEFINITE: `--> Slip:D` refused it (highlighter's
#     `matches`), as did `Empty ~~ Slip:D`. Its `.defined` stays False.
#  5. `$x := %h<k>:delete` bound the value and never deleted: rak consumes
#     App::Rak's options that way, and App::Rak warned "Unexpected leftovers".
#  6. A Lock boolified False (its state is not in the map behind it): rak runs
#     a pattern's NEXT as `$lock.protect(&next-phaser) if $lock`, so never did.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- 1. nqp::istype and Whatever --------------------------------------------
{
    my $p = *;
    ck(nqp::istype($p, Whatever), 1, 'nqp::istype(*, Whatever)');
    ck(nqp::istype(42, Whatever), 0, '…and not for an Int');
    sub stem(str $basename, $parts = *) {
        (my @indices := indices($basename, '.'))
          ?? nqp::substr($basename, 0,
               nqp::istype($parts, Whatever) || $parts > @indices
                 ?? @indices[0]
                 !! @indices[@indices - $parts])
          !! $basename
    }
    ck((stem("foo.tar.gz"), stem("foo.tar.gz", 1), stem("foo.tar.gz", *)),
       ('foo', 'foo.tar', 'foo'), 'String::Utils\' stem');
}

# --- 2. Cool:D against a bare @ ---------------------------------------------
{
    multi a(Cool:D $c) { 'cool' }
    multi a(@a)        { 'list' }
    ck((a("x".lines), a([1, 2]), a((1, 2)), a("s")), ('list', 'list', 'list', 'cool'),
       '@ beats Cool:D for a Seq, an Array and a List; a Str stays Cool');
    multi d(Cool:D $c) { 'cool' }
    multi d(%h)        { 'hash' }
    ck(d({ a => 1 }), 'hash', '% beats Cool:D for a Hash');
    multi b(Any:D $c) { 'any' }
    multi b(Cool $c)  { 'cool' }
    ck(b([1]), 'cool', 'Cool still beats Any:D');
    my proto p(|) {*}
    my multi p(@source, Int:D $initial = 0) { "list:" ~ @source.elems }
    my multi p(Cool:D $string, Int:D $initial = 0) { "cool -> " ~ p($string.Str.lines, $initial) }
    ck(p("a\nb"), 'cool -> list:2', 'paragraphs\' shape: the string candidate hands on to the list one');
}

# --- 3. a role's preset is there before its TWEAK ---------------------------
{
    my @seen;
    my role Type { has $.type; method TWEAK() { @seen.push($!type) } }
    my $a = "bar" but Type<words>;
    my $b = "bar" but Type("contains");
    ck((@seen.List, $a.type, $b.type), (<words contains>, 'words', 'contains'),
       'but R<value> / R(value): TWEAK sees the value');
}

# --- 4. Empty is DEFINITE ---------------------------------------------------
{
    ck((Empty.defined, Empty.DEFINITE, Empty ~~ Slip:D), (False, True, True),
       'Empty: not .defined, but DEFINITE and a Slip:D');
    sub f(--> Slip:D) { Empty }
    ck(f().elems, 0, '--> Slip:D takes Empty');
    sub g(Slip:D $s) { $s.elems }
    ck(g(Empty), 0, '…and so does a Slip:D parameter');
}

# --- 5. := of an adverbed subscript -----------------------------------------
{
    my %h = a => 0, b => 1;
    my $x; $x := %h<a>:delete;
    ck(($x, %h.keys.List), (0, ('b',)), ':= of %h<k>:delete deletes');
    my @a = 1, 2, 3;
    my $y := @a[1]:delete;
    ck(($y, @a[1].defined), (2, False), '…and of @a[i]:delete');
    my %g = k => 1;
    my $e := %g<k>:exists;
    ck($e, True, ':= of :exists');
    my $z := %g<k>; $z = 5;
    ck(%g<k>, 5, 'a plain := still binds the slot');
}

# --- 6. a Lock is true ------------------------------------------------------
{
    ck((so Lock.new, so Lock::Async.new, so Lock.new.condition, so Lock), (True, True, True, False),
       'Lock, Lock::Async and a condition are true; the type object is not');
    my $n = 0;
    my $lock := Lock.new if True;
    $lock.protect({ $n++ }) if $lock;
    ck($n, 1, 'rak\'s `$lock.protect(&next-phaser) if $lock` runs');
}

say $fails ?? "FAILED $fails" !! "PASS";
