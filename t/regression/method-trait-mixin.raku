# The `is constraint(…)` shape: a user `trait_mod:<is>` that takes the Method and
# mixes a marker role into it. Path::Finder tags every matcher method that way and
# then refuses any key whose method is not `~~ Constraint` (issue #34).
#
# Five separate things had to hold for that to work:
#   1. `$obj does R($v)` — a role with exactly ONE attribute is instantiated with a
#      positional that presets it. This reached the default constructor, which
#      takes named arguments only, so the form died outright.
#   2. `does` on a ROUTINE mixes IN PLACE. The class's method table shares the
#      Callable, so that is what makes the mixin visible to a later `^lookup`;
#      boxing into a fresh object left the trait's work unreachable.
#   3. The handler is often declared in the class's OWN body (a `unit class` keeps
#      it beside the methods), so the body's subs — and the enums its signature is
#      typed by — must exist before the method traits run.
#   4. `trait_mod:<of>($method, T)`, which such a handler commonly delegates to
#      first, has to be callable at all: both the NAME had to parse as a call and
#      the routine had to exist.
#   5. A trait on a `proto` belongs to the DISPATCHER — what `^lookup` returns —
#      and that dispatcher has to type as a Method, or a handler declared
#      `(Method $m, …)` will not bind it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- 1 + 2: the mixin forms, on their own ------------------------------------
{
    enum Prec <Lo Hi>;
    role Mark { has Prec:D $.precedence is required; }
    class Plain { }
    my $o = Plain.new does Mark(Hi);
    check ($o ~~ Mark),   True, 'does R($v) on a plain object';
    check $o.precedence,  Hi,   '…presets the single attribute';
    my $n = 5 but Mark(Lo);
    check $n.precedence,  Lo,   'but R($v) too';
    check ($n == 5),      True, '…and `but` keeps the base value';

    my $sub = sub f() { 42 };
    my $mixed = $sub does Mark(Hi);
    check ($sub ~~ Mark),   True, 'does on a ROUTINE mixes in place: the original sees it';
    check ($mixed ~~ Mark), True, '…and so does the returned value';
    check $sub.precedence,  Hi,   'the role accessor answers on the routine';
    check $sub(),           42,   'and the routine still runs';
}

# --- 3 + 4 + 5: the whole trait shape, handler inside the class body ---------
{
    class Finder {
        enum Precedence <Skip Depth Name Stat>;
        role Constraint { has Precedence:D $.precedence is required; }
        multi sub trait_mod:<is>(Method $method, Precedence:D :$constraint!) {
            trait_mod:<of>($method, Mu);               # what Path::Finder does first
            return $method does Constraint($constraint);
        }
        method file(Bool $v = True) is constraint(Stat) { 'file' }
        proto method path(Mu $p) is constraint(Depth) { * }
        multi method path(Str $p) { 'path' }
        method plain() { 'plain' }
    }
    my $file = Finder.^lookup('file');
    check ($file ~~ Finder::Constraint), True, 'a handler declared in the class body fires';
    check $file.precedence, Finder::Stat, '…and the precedence it presets is readable';
    my $path = Finder.^lookup('path');
    check ($path ~~ Finder::Constraint), True, "a trait on a `proto` tags what ^lookup returns";
    check $path.precedence, Finder::Depth, '…with its own precedence';
    check (Finder.^lookup('plain') ~~ Finder::Constraint), False, 'an untagged method stays untagged';
    check Finder.new.file, 'file', 'the tagged method still runs';
    check Finder.new.path('x'), 'path', 'the tagged proto still dispatches';
}

# a method group's dispatcher is a Method, not a Sub
{
    class G { proto method m($x) { * }; multi method m(Int $x) { 1 } }
    check G.^lookup('m').WHAT.^name, 'Method', 'the dispatcher of a method group is a Method';
}

# `trait_mod:<of>` is callable by name
{
    class T { method m() { 1 } }
    my $ok = True;
    try { trait_mod:<of>(T.^lookup('m'), Int); CATCH { default { $ok = False } } }
    check $ok, True, 'trait_mod:<of>($routine, Type) parses and runs';
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
