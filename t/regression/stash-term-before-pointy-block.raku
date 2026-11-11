# Regression: a package's stash term — a name with a trailing `::` — just
# before a block or a pointy block was read as a routine that took the
# block as its argument, so `for A::, B:: -> $pkg { … }` died "Function
# 'B::' needs parens to avoid gobbling block" (issue #238, a 5.6.0
# regression). Re-exporting several modules' EXPORT::DEFAULT stashes is the
# shape it arrived in. A lone stash is iterated entry by entry, as a hash
# is; a name nothing declares is still the gobbled-block error.
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppStashTermFoo;
use RakuppStashTermBar;

my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my @keys;
for RakuppStashTermFoo::EXPORT::DEFAULT::, RakuppStashTermBar::EXPORT::DEFAULT:: -> $pkg {
    @keys.append: $pkg.keys;
}
check @keys.sort.List, ('&stash-bar', '&stash-foo'), 'two modules\' EXPORT::DEFAULT stashes';

class A { our sub x { }; our $y = 1 }
class B { }
my @seen;
for A::, B:: -> $pkg { @seen.push: $pkg.^name }
check @seen.List, ('Stash', 'Stash'), 'two stashes are two items';
@seen = ();
for A:: -> $p { @seen.push: $p.key }
check @seen.sort.List, ('$y', '&x'), 'one stash is its entries';
@seen = ();
for A:: { @seen.push: .key }
check @seen.sort.List, ('$y', '&x'), '…without a signature';
@seen = ();
for B:: -> $p { @seen.push: $p }
check @seen.List, (), 'an empty one runs nothing';
@seen = ();
for A::, (B::) -> $pkg { @seen.push: 1 }
check @seen.elems, 2, 'the parenthesised workaround still works';
my $s = A::;
@seen = ();
for $s -> $p { @seen.push: 1 }
check @seen.elems, 1, 'an itemized stash is one item';

try EVAL 'for NoSuchPackageAnywhere:: -> $p { }';
check $!.^name, 'X::Comp::Group', 'an undeclared one is refused';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
