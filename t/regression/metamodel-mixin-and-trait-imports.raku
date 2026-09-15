# Regression: six engine gaps that stood between rakupp and Red (issue #77), all
# of them silent — each one let the program carry on and answer nothing.
#
#   1. `$obj does Role(arg)` presets the role's single PUBLIC attribute. Counting
#      EVERY attribute refused a role with a private one beside its public (which
#      is Red::Attr::Column exactly) and preset the PRIVATE one for a role that
#      had only that.
#   2. A role mixed into an ATTRIBUTE meta-object could not read or write its own
#      private attributes, so a trait's own state was invisible to its methods.
#   3. …and its private attributes were seeded under their bare names, where they
#      SHADOWED a role method of the same name (`has $!column` over `method column`).
#   4. A module import could drop a bare type-object placeholder over a routine
#      already in scope: after `use NativeLibs`, every user-defined `is` trait in
#      the file silently stopped being applied.
#   5. A signature TYPE CAPTURE (`::T $x`) never bound, so the body's `T` was a
#      type object literally named "T".
#   6. `.^add_multi_method` did not exist.
#
# Every expectation below was checked against Rakudo.
#
# Gap 4 needs a module that re-exports NativeCall's export list — NativeLibs
# is the one the original report used, and the `use` below is what arms the
# whole file. Declared, so a machine without it skips instead of dying on the
# import: CI installs no ecosystem modules, and there this case cannot run.
#?requires NativeLibs

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppTraitProbe;
# …and then a module that re-exports NativeCall's export list, which carries
# `&trait_mod:<is>` as a bare placeholder. This import used to drop that
# placeholder over the trait imported above, and every `is probed` below then did
# nothing at all — silently. Order matters: this has to come AFTER.
use NativeLibs;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- 1. `does Role(arg)` binds the single PUBLIC attribute -----------------
role OnePub  { has %.args; has $!hidden; method rd() { %!args<n> // '-' } }
role TwoPub  { has %.one;  has %.two; }
role PrivOnly { has $!only; }
role NoAttrs  { method rd() { 'none' } }
class Plain { }
my $a = Plain.new; $a does OnePub(%(n => 'v'));
ck($a.rd, 'v',                        'a private attribute beside the public one is no obstacle');
ck($a.args<n>, 'v',                   '…and the public one is what got set');
# (spelled out one by one: `$obj does $role-in-a-variable(arg)` is a COERCION in
# Rakudo, not this form, so a loop over the roles would test something else)
my $e1 = ''; { my $o = Plain.new; $o does TwoPub(1);   CATCH { default { $e1 = .^name } } }
my $e2 = ''; { my $o = Plain.new; $o does PrivOnly(1); CATCH { default { $e2 = .^name } } }
my $e3 = ''; { my $o = Plain.new; $o does NoAttrs(1);  CATCH { default { $e3 = .^name } } }
ck($e1, 'X::Role::Initialization', 'two public attributes: refused');
ck($e2, 'X::Role::Initialization', 'only a private one: refused');
ck($e3, 'X::Role::Initialization', 'no attributes at all: refused');

# --- 2+3. a role mixed into an ATTRIBUTE meta-object ----------------------
class Marked { has Str $.field is probed{ :n<deep> }; }
my $attr = Marked.^attributes.first(*.name eq '$!field');
ck(?($attr ~~ ProbeRole), True,       'the trait mixed its role into the attribute');
ck($attr.peek-private, 'deep',        'a role method reads its own PRIVATE attribute');
ck($attr.args<n>, 'deep',             '…and the public one through its accessor');
ck($attr.slot, 'SLOT',                'a private attribute does not shadow the method of that name');
ck($attr.bump, 2,                     'a role method WRITES its own private attribute');

# --- 4. an import must not silently disarm a trait -------------------------
# (`use NativeLibs` sits above, after the trait's own module; the checks in this
# section run through that same trait, so their passing IS the proof.)
class Later { has Str $.z is probed; }
ck(?(Later.^attributes[0] ~~ ProbeRole), True,
                                      'a trait still applies after a module that re-exports NativeCall');

# --- 5. signature type captures -------------------------------------------
sub tc1(::T $x) { T.^name }
sub tc2(::T \v) { T.^name }
sub tc3(::T Int:D \v) { T.^name }
ck(tc1(42), 'Int',                    'a type capture binds the argument type');
ck(tc2('s'), 'Str',                   '…for a sigilless parameter too');
ck(tc3(7), 'Int',                     '…and beside a real constraint');
sub tc4(::T Int:D $x) { my Int $y = 5; T.^name ~ "/" ~ $y }
ck(tc4(3), 'Int/5',                   'the constraint is not itself captured as the name');

# --- 6. .^add_multi_method -------------------------------------------------
class Multi { }
Multi.^add_multi_method('red-pick', my method (Int:D $x) { "int:$x" });
Multi.^add_multi_method('red-pick', my method (Str:D $x) { "str:$x" });
Multi.^compose;
ck(Multi.red-pick(3), 'int:3',            'add_multi_method installs a candidate');
ck(Multi.red-pick('a'), 'str:a',          '…and a second one joins the same group');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
