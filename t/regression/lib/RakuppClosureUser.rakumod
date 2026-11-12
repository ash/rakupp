# t/regression/exe-module-closure-lexicals.raku: blocks built in this module's
# routines and run by RakuppClosureWalker's, reading this module's lexicals.
unit module RakuppClosureUser;
use RakuppClosureWalker;
my constant SKIP = <b c>;
my constant FLAG = 'c';
my $SKIPV = <b c>;
sub is-skip($n) { $n eq 'b' | 'c' }
sub t-own-sub    is export { keep(<a b c d>, prune => -> $n { so is-skip($n) }) }
sub t-own-amp    is export { my &f = &is-skip; keep(<a b c d>, prune => -> $n { so f($n) }) }
sub t-amp-in-blk is export { keep(<a b c d>, prune => -> $n { so &is-skip($n) }) }
sub t-constant   is export { keep(<a b c d>, prune => -> $n { $n ∈ SKIP }) }
sub t-str-const  is export { keep(<a b c d>, prune => -> $n { $n eq FLAG }) }
sub t-method     is export { Keeper.new.keep(<a b c d>, -> $n { $n ∈ SKIP }) }
sub t-whatever   is export { keep(<a b c d>, prune => * ∈ SKIP) }
sub t-lexical    is export { my $s = SKIP; keep(<a b c d>, prune => -> $n { $n ∈ $s }) }
sub t-my-var     is export { keep(<a b c d>, prune => -> $n { $n ∈ $SKIPV }) }
sub flat-pairs(@p)  is export { @p.map(|*.list).List }
sub flat-whole(@p)  is export { @p.map(|*).List }
