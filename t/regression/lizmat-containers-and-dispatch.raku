# Regression: the second half of the lizmat ecosystem batch — the engine bugs
# that turned up once the missing nqp ops (t/regression/lizmat-nqp-ops.raku)
# stopped hiding them. Each one gated a distribution that several others wait on.
#
#   * `.BIND-KEY($k, $value)` left the element WRITABLE. Only the `%h<k> := v`
#     spelling marked it, and Hash::Agnostic is written entirely in the method
#     spelling — seven dists sit behind it.
#   * an attribute named `$!reified` was read as the engine's own backing-store
#     name, so `nqp::bindattr($obj, T, '$!reified', $buf)` REPLACED the object
#     with a bare Array (ReverseIterables, and OneSeq behind it)
#   * `:!exists` on a pseudo-package stash subscript would not parse
#   * a Code object answered neither `.file` nor `.line`, so a module deciding
#     its export list by source file exported nothing (Identity::Utils)
#   * `.subst('LITERAL')` read a leading `:` in the needle as a regex ADVERB —
#     `:ver` threw, and `:x` silently ate the needle and substituted nothing
#   * a TYPE OBJECT bound a native `str`/`int` parameter, so `say Int` under a
#     module that redefines say with native candidates printed an empty line
#
# Runs clean under Rakudo too, which is the only thing that makes it worth having.

use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub dies-with(&c) { my $t = 'did NOT die'; try { c(); CATCH { default { $t = 'died' } } }; $t }

# ---- BIND-KEY makes the element immutable ------------------------------
# The marker is per-row so one key's verdict cannot stand in for another's.
my %h;
%h.BIND-KEY('bound', 137);
ck %h<bound>, 137, 'BIND-KEY stores the value';
ck dies-with({ %h<bound> = 666 }), 'died', 'assigning to a BIND-KEY element dies';
ck %h<bound>, 137, '…and the element is unchanged';
ck (%h<bound>:delete), 137, '…and it can still be deleted';

# binding something that NAMES a container aliases it instead, and writing
# through that alias is the whole point — this half must NOT die
my $cell = 5;
%h.BIND-KEY('alias', $cell);
$cell = 9;
ck %h<alias>, 9, 'BIND-KEY of a container aliases it';
ck dies-with({ %h<alias> = 1 }), 'did NOT die', '…and an alias stays writable';

# an ordinary assignment is untouched
%h.ASSIGN-KEY('plain', 1);
ck dies-with({ %h<plain> = 2 }), 'did NOT die', 'ASSIGN-KEY leaves the element writable';
ck %h<plain>, 2, '…and the write lands';

# the same through a user class that delegates, which is Hash::Agnostic's shape
class MyHash {
    has %!hash;
    method AT-KEY($key)           is raw { %!hash.AT-KEY($key)            }
    method BIND-KEY($key, \value) is raw { %!hash.BIND-KEY($key, value)   }
    method EXISTS-KEY($key)              { %!hash.EXISTS-KEY($key)        }
    method DELETE-KEY($key)              { %!hash.DELETE-KEY($key)        }
    method keys()                        { %!hash.keys                    }
}
my %m is MyHash;
%m<w> = 1;
ck %m<w>, 1, 'a delegating hash class stores an ordinary assignment';
(%m<b> := 137);
ck %m<b>, 137, '…and a bind through its own BIND-KEY';
ck dies-with({ %m<b> = 666 }), 'died', '…whose element is then immutable';
ck %m<b>, 137, '…and keeps the bound value';

# ---- `$!reified` is an ordinary attribute name -------------------------
# The value bound is an IterationBuffer, which is exactly what the engine's own
# backing-store rebind expects to see — so a wrong guard cannot pass this.
my class Holder {
    has $!reified;
    method make() {
        my $buf := nqp::create(IterationBuffer);
        $buf.push('kept');
        my $self := nqp::create(self);
        nqp::bindattr($self, self, '$!reified', $buf);
        $self
    }
    method peek() { nqp::atpos(nqp::getattr(self, Holder, '$!reified'), 0) }
}
my $held = Holder.make;
ck $held.^name, 'Holder', 'binding $!reified leaves the OBJECT intact';
ck $held.peek, 'kept', '…and the attribute holds what was bound';

# ---- `:!exists` on a pseudo-package stash ------------------------------
my $declared = 1;
ck (MY::{'$declared'}:exists), True, 'MY:: :exists finds a declared symbol';
ck (MY::{'$declared'}:!exists), False, 'MY:: :!exists is its negation';
ck (MY::{'$never-declared-here'}:!exists), True, '…and answers True for an absent one';

# ---- a Code object knows where it was declared -------------------------
sub a-named-sub() { 42 }
ck a-named-sub(), 42, 'the probe sub runs';
ck &a-named-sub.file.ends-with('lizmat-containers-and-dispatch.raku'), True,
   '&sub.file names the file it was declared in';
ck (&a-named-sub.line ~~ Int && &a-named-sub.line > 0), True,
   '&sub.line answers a line number';

# ---- .subst with a LITERAL needle --------------------------------------
# Each row carries a different metacharacter, so one working spelling cannot
# stand in for the rest.
my $id = 'Foo:xa<b>:ver<1>bar';
ck $id.subst(':x'), 'Fooa<b>:ver<1>bar', '.subst removes a literal starting with a colon';
ck $id.subst(':ver'), 'Foo:xa<b><1>bar', '…even when what follows is an adverb name';
ck $id.subst(':ver<1>'), 'Foo:xa<b>bar', '…angle brackets and all';
ck $id.subst('<x>'), $id, '…and a needle that is absent changes nothing';
ck 'a1b2'.subst(/\d/, 'X', :g), 'aXbX', 'a real regex needle still works';
ck 'a.b.c'.subst('.'), 'ab.c', 'a literal dot is a dot, not "any character"';

# ---- a type object does not bind a native parameter --------------------
multi sub nat(str $s) { 'STR-NATIVE' }
multi sub nat(Str:D $s) { 'STR-D' }
multi sub nat($s)     { 'ANY' }
ck nat(Int), 'ANY', 'a type object does not bind a native str parameter';
ck nat(Str), 'ANY', '…not even its own type';
ck nat('x'), 'STR-D', '…while a real string still reaches the Str:D candidate';

# ---- a role's TYPE-CAPTURE parameter reaches its attributes -------------
# `role R[::TYPE] { has TYPE @!items }` left the attribute's declared type
# reading the literal name "TYPE", so the typed container refused EVERY value
# put into it — in an R[Int] exactly as in an R[Str] — and `.of` answered
# "TYPE". Concurrent::PriorityQueue is built on that shape. The role's
# ClassInfo is SHARED by every class composing it, so the name is resolved per
# object rather than rewritten in place; R[Int] and R[Str] below are the two
# rows that prove the sharing is respected.
role Keeper[::TYPE = Any] {
    has TYPE @.items;
    method add($x) { @!items.push($x); self }
}
class KInt does Keeper[Int] { }
class KStr does Keeper[Str] { }
class KAny does Keeper { }

sub took($obj, $v) { my $r = 'took'; try { $obj.add($v); CATCH { default { $r = 'refused' } } }; $r }
ck took(KInt.new, 42), 'took', 'a role-typed attribute accepts its bound type';
ck took(KInt.new, 'str'), 'refused', '…and refuses another';
ck took(KStr.new, 'str'), 'took', 'a DIFFERENT binding of the same role accepts its own type';
ck took(KStr.new, 42), 'refused', '…and refuses the first one`s';
ck took(KAny.new, 42), 'took', 'a bare `does` takes the capture DEFAULT, so anything goes';
ck took(KAny.new, 'str'), 'took', '…including a string';
ck KInt.new.add(1).add(2).items.elems, 2, 'the accepted values are really stored';

# ---- `.^mro(:roles)` puts the roles back in -----------------------------
# The plain form excludes them; `are` walks the role-inclusive one to find the
# common type of a list, and answered Cool where Real was due.
ck Int.^mro.map(*.^name).join(' '), 'Int Cool Any Mu', '.^mro excludes roles';
ck Int.^mro(:roles).map(*.^name).join(' '), 'Int Real Numeric Cool Any Mu',
   '.^mro(:roles) includes them';
ck Rat.^mro(:roles).map(*.^name).join(' '), 'Rat Rational Real Numeric Cool Any Mu',
   '…for Rat too, Rational and all';
ck Str.^mro(:roles).map(*.^name).join(' '), 'Str Stringy Cool Any Mu', '…and Stringy for Str';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
