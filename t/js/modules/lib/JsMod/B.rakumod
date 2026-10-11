# A sub named `helper` again — private to this module, not A's — and calls
# into A by its exports and by its package name.
use JsMod::A;
unit module JsMod::B;
sub helper($x) { "B:$x" }
sub h($x) is export { helper(f($x)) ~ " / " ~ JsMod::A::g() ~ " / K=" ~ JsMod::A::K }
