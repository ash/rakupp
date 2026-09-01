# Regression: `.clone` diverged from Rakudo in five ways, and the two dispatch
# gaps a user-written `clone` runs into.
#
# Found by diffing ~130 clone cases against Rakudo 2026.08, 2026-09-01:
#
#   1. A Pair cloned to ITSELF. VT::Pair fell into the "immutable — clone is the
#      value" arm of the non-object clone, but every copy of a Pair Value shares
#      the one cell its `.value` points at, so `$p.clone.value = 7` rewrote $p.
#   2. A `@`/`%` attribute twiddle BOUND its raw value instead of assigning into
#      the attribute's container: `.clone(a => 5)` left `@!a` holding 5, and
#      `.clone(a => (1,2))` left a List that `.push` could not touch.
#   3. Twiddles reached PRIVATE attributes. Rakudo twiddles only attributes with
#      accessors — `is built` opens construction by name, not cloning.
#   4. A POSITIONAL argument was silently ignored (`clone(*%twiddles)` takes
#      named arguments only; Rakudo cannot resolve the call).
#   5. A Routine/Block cloned to itself, so the clone SHARED the origin's `state`
#      variables instead of starting fresh.
#
# and, adjacent to it:
#
#   6. `callsame`/`nextsame` from a user method that overrides a BUILT-IN died
#      with "not in the dynamic scope of a dispatcher" — invokeMethodChain only
#      set up a redispatch frame when a user ancestor or a native base defined
#      the name. A method now leaves a breadcrumb to the built-in behind it
#      (ExecContext::builtinFallback) rather than paying for a frame per call.
#   7. `=:=` answered by CONTENTS whenever an operand was not a plain variable,
#      so `@a.clone =:= @a` and even `[1,2] =:= @a` were True.
#
# Contract: exit 0 + last line PASS. Passes under Rakudo too — every check here
# was taken from its answers.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- 1. a Pair clones to a DISTINCT pair ------------------------------------
{
    my $p = Pair.new('a', my $v = 1);
    my $q = $p.clone;
    $q.value = 7;
    check $p.value, 1, 'the original pair keeps its value';
    check $q.value, 7, 'the clone carries the new one';
}

# --- 2. container attributes: a twiddle ASSIGNS, it does not bind ------------
class Cont { has @.a; has %.h; has $.s }
{
    check Cont.new.clone(a => 5).a,        [5],      'a scalar twiddle fills @.a';
    check Cont.new.clone(a => (3, 4)).a,   [3, 4],   'a List twiddle becomes an Array';
    check Cont.new.clone(a => (3,4)).a.WHAT.^name, 'Array', '…and it IS an Array';
    check Cont.new.clone(h => (x => 1)).h, {x => 1}, 'a Pair twiddle becomes a Hash';
    check Cont.new.clone(h => (x=>1)).h.WHAT.^name, 'Hash', '…and it IS a Hash';
    # the cloned container is the clone's own, and writable
    my $c = Cont.new(a => [1]).clone(a => (3, 4));
    $c.a.push(9);
    check $c.a, [3, 4, 9], 'the cloned array takes a push';
    # a scalar attribute still takes whatever it was given
    check Cont.new.clone(s => [1,2]).s, [1,2], 'a $ attribute is not coerced';
}

# --- 3. only PUBLIC attributes are twiddled ---------------------------------
{
    my class Priv { has $!s = 1; has $.p = 2; method g { $!s } }
    my $c = Priv.new.clone(s => 99, p => 88);
    check $c.g, 1,  'a private attribute is not twiddled';
    check $c.p, 88, 'a public one is';
    # `is built` opens CONSTRUCTION by name and nothing else
    my class Built { has $!b is built; method g { $!b } }
    check Built.new(b => 1).clone(b => 5).g, 1, 'is built does not open cloning';
    # an unknown name is ignored, not stored (it must not shadow `$.p` later)
    check Priv.new.clone(nope => 5).p, 2, 'an unknown twiddle is ignored';
}

# --- 4. clone takes named arguments only ------------------------------------
{
    my $died = False;
    my $msg = '';
    try { Cont.new.clone(5); CATCH { default { $died = True; $msg = .message } } }
    check $died, True, 'a positional argument to clone dies';
    check $msg.starts-with('Cannot resolve caller clone('), True,
          '…with the no-matching-signature message';
}

# --- 5. a cloned routine gets FRESH state -----------------------------------
{
    my $f = sub { state $n = 0; ++$n };
    $f(); $f();
    check $f(),         3, 'the original keeps counting';
    check $f.clone.(),  1, 'the clone starts over';
    check $f(),         4, '…without disturbing the original';
    my $b = { state $m = 0; ++$m };
    $b(); 
    check $b.clone.(), 1, 'a Block clones the same way';
}

# --- 6. deferring from a user method into the BUILT-IN behind it -------------
{
    my class Twiddler {
        has $.x is rw;
        method clone(*%t) { my $c = callsame(); $c.x = $c.x + 100; $c }
    }
    check Twiddler.new(x => 1).clone.x, 101, 'callsame reaches the built-in clone';

    my class Nexter {
        has $.x is rw;
        method clone(*%t) { nextsame }
    }
    check Nexter.new(x => 1).clone(x => 5).x, 5, 'nextsame does too, with twiddles';

    my class Wither {
        has $.v;
        method clone(*%t) { callwith(v => 99) }
    }
    check Wither.new(v => 1).clone.v, 99, 'callwith passes its own arguments on';

    # the same gap, on the two stringifiers people override most
    my class Stringy { method Str { nextsame } }
    check Stringy.new.Str.starts-with('Stringy'), True, 'method Str { nextsame }';
    my class Gisty { method gist { callsame } }
    check Gisty.new.gist.starts-with('Gisty'), True, 'method gist { callsame }';

    # a user chain still wins over the built-in, and still reaches it underneath
    my class Base { has $.x; method clone(*%t) { callsame() } }
    my class Derived is Base { method clone(*%t) { callsame() } }
    check Derived.new(x => 3).clone.x, 3, 'a two-deep user chain lands on the built-in';
}

# --- 7. `=:=` is identity, not equality -------------------------------------
{
    my @a = 1, 2;
    my %h = a => 1;
    my $o = Cont.new;
    sub f { @a }
    check (@a.clone =:= @a), False, 'a cloned array is a different array';
    check ([1, 2]  =:= @a),  False, 'an equal literal array is not @a';
    check (@a      =:= @a),  True,  'an array is itself';
    check (f()     =:= @a),  True,  '…however it arrives';
    check (%h.clone =:= %h), False, 'a cloned hash is a different hash';
    check ((a => 1) =:= (a => 1)), False, 'two equal pairs are two pairs';
    check ($o.clone =:= $o), False, 'a cloned object is a different object';
    check (&f.clone =:= &f), False, 'a cloned routine is a different routine';
    check (Int =:= Int),     True,  'a type object is itself';
}

# --- the invariants around all of that, which must stay ---------------------
{
    # clone does NOT re-run BUILD/TWEAK
    my $tweaks = 0;
    my class T { has $.x is rw; submethod TWEAK { $tweaks++ } }
    my $t = T.new(x => 1);
    $t.clone; $t.clone(x => 9);
    check $tweaks, 1, 'clone runs neither BUILD nor TWEAK';

    # the copy is SHALLOW: a container attribute is shared with the original
    my class Holder { has @.a }
    my $h1 = Holder.new(a => [1, 2]);
    my $h2 = $h1.clone;
    $h2.a.push(3);
    check $h1.a, [1, 2, 3], 'an untwiddled container attribute stays shared';

    # …but the attribute SLOTS are the clone's own
    my class Slot { has $.x is rw }
    my $s1 = Slot.new(x => 1);
    my $s2 = $s1.clone;
    $s2.x = 9;
    check $s1.x, 1, 'writing the clone does not write the original';

    # inheritance, roles and mixins all twiddle
    my role R { has $.r is rw }
    my class P2 { has $.p }
    my class C2 is P2 does R { has $.c }
    my $k = C2.new(p => 1, r => 2, c => 3).clone(p => 10, r => 20);
    check ($k.p, $k.r, $k.c), (10, 20, 3), 'inherited and role attributes twiddle';
    my $mixed = (P2.new(p => 1) but role Q { has $.q is rw = 5 }).clone(q => 9);
    check $mixed.q, 9, 'a mixin attribute twiddles';

    # containers clone independently, and a Date validates its twiddles
    my @arr = 1, 2;
    my @cop = @arr.clone;
    @cop.push(3);
    check @arr, [1, 2], 'Array.clone is independent';
    my %hsh = a => 1;
    my %cph = %hsh.clone;
    %cph<b> = 2;
    check %hsh, {a => 1}, 'Hash.clone is independent';
    check Date.new(2020, 1, 31).clone(year => 2021).gist, '2021-01-31', 'Date.clone twiddles';
    my $bad = False;
    try { Date.new(2020,1,31).clone(month => 13); CATCH { default { $bad = True } } }
    check $bad, True, 'Date.clone still validates';

    # an immutable value clones to itself
    check (42.clone, "s".clone, True.clone), (42, "s", True), 'immutables clone to themselves';
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
