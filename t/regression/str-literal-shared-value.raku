# Guard: a string literal's value is shared by every evaluation, and no write
# to a copy of it reaches the literal.
#
# A StrLit builds its Value once and copies it on every later evaluation
# (StrLit::evalState in src/Ast.h, VALUE32-PLAN "A string literal keeps its
# Value"). A literal over 15 bytes is a shared body, and every evaluation's copy
# holds that same body, so a write made in place on a copy would change the
# literal for the rest of the run. Each write below goes through a copy, then
# the literal is evaluated again.

sub check($got, $want, $what) {
    die "str-literal-shared-value: $what: got {$got.raku}, expected {$want.raku}" unless $got eqv $want;
}

constant LONG  = 'a literal of more than fifteen bytes';
constant SHORT = 'short';
sub long()  { "a literal of more than fifteen bytes" }
sub short() { "short" }
sub accent() { "héllo wörld, long enough to share" }

for &long, LONG, &short, SHORT -> &lit, $want {
    my $x = lit(); $x ~= "!";                  check lit(), $want, "~= on a copy";
    my $y = lit(); $y ~~ s/l/L/;               check lit(), $want, "s/// on a copy";
    my $z = lit(); $z.substr-rw(0, 1) = "Z";   check lit(), $want, "substr-rw on a copy";
    my $u = lit(); $u .= uc;                   check lit(), $want, ".= on a copy";
    my $w = lit(); $w.subst-mutate(/s/, "S");  check lit(), $want, "subst-mutate on a copy";
    my $b = Buf.new(lit().encode); $b[0] = 65; check lit(), $want, "a Buf made from it, written";
    my $p = lit(); $p = "x" ~ $p;              check lit(), $want, "prepend to a copy";
    sub bump($s is rw) { $s ~= "?" }
    my $r = lit(); bump($r);                   check lit(), $want, "an is-rw argument appended to";
    my @a = lit() xx 3; @a[1] ~= "#";          check @a[0], $want, "an array element beside a written one";
}

# a non-ASCII literal: a combining mark appended to a copy joins its last
# character there, and the literal keeps its text and its count
my $c = accent(); $c ~= "\x[0301]";
check accent(), "héllo wörld, long enough to share", "a non-ASCII literal after a join on a copy";
check accent().chars, 33, "its .chars";

# first evaluations from many threads at once: every thread gets the same text
sub fresh() { "evaluated first by several threads at the same time" }
my @got = await (^8).map: { start { fresh() } };
check @got.unique.List, ("evaluated first by several threads at the same time",), "a literal first evaluated in parallel";

say "PASS";
