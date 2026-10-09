# Regression, 2026-10-09: a Seq given to an `@` parameter binds as the List
# it caches into (PositionalBindFailover), and the parameter's `where` sees
# that List, not the Seq. Multi dispatch ran the `where` on the raw Seq, so
# Math::SparseMatrix 0.0.15 failed its own t/26-dot-product.rakutest:
# `CSR.new(dense-matrix => @vec.map({[$_,]}))` with
# `:@dense-matrix! where @dense-matrix ~~ List:D && ….all ~~ List:D` fell
# through to Mu.new ("The attribute '$!nrow' is required"). A `where` that
# iterated the Seq also consumed it, and the candidate that won next died with
# X::Seq::Consumed. Expected values are Rakudo 2026.09's.
use Test;
plan 15;

# the dist's own shape
class CSR {
    has $.nrow is required;
    multi method new(:@dense-matrix! where @dense-matrix ~~ List:D && @dense-matrix.all ~~ List:D) {
        self.bless(nrow => @dense-matrix.elems)
    }
}
my @vec = 1, 2, 3;
is (try CSR.new(dense-matrix => @vec.map({[$_,]})).nrow), 3, 'a named @-param\'s where sees a Seq as a List';

multi k(:@x! where @x ~~ List:D) { @x.^name }; multi k(*%) { 'fallback' }
is k(x => (1, 2).map({$_})), 'List', 'a named @-param, smartmatch where';

multi s(@x where @x ~~ List:D) { @x.^name }; multi s(*@) { 'fallback' }
is s((1, 2).map({$_})), 'List', 'a positional @-param, smartmatch where';

multi t(@x where *.^name eq 'List') { 'list' }; multi t(*@) { 'fallback' }
is t((1, 2).map({$_})), 'list', 'a positional @-param, WhateverCode where';

multi y(@x where { $_ ~~ List:D }) { 'list' }; multi y(*@) { 'fallback' }
is y((1, 2).map({$_})), 'list', 'a positional @-param, block where';

# a `where` that reads the Seq caches it: the candidate that wins next can
# still read it
multi d(@x where @x.sum > 100) { 'big' }; multi d($y) { $y.list.join(',') }
is d((1, 2, 3).map(* * 2)), '2,4,6', 'a failing where leaves the Seq readable';

my $sq = (1, 2, 3).map(* * 2);
multi c(@x where @x.elems == 3) { @x.sum }; multi c(*@) { 'fallback' }
is c($sq), 12, 'a passing where, then the bind';
is $sq.list.join(','), '2,4,6', '…and the caller\'s Seq is still readable';

# lazy: the where and the bound parameter both see a List
multi a(@x where *.^name eq 'List') { @x.^name ~ ' ' ~ @x.is-lazy }; multi a(*@) { 'fallback' }
is a((1..*).map(* + 1)), 'List True', 'a lazy Seq: where and bind see a List';
sub b(@x) { @x.^name }
is b((1..*).map(* + 1)), 'List', 'a lazy Seq binds to @x as a List';

# a Seq held in a `$` binds as a List too, eager or lazy, positional or named
my $es = (1, 2).map(* + 1);
my $ls = (1..*).map(* + 1);
sub n(:@x) { @x.^name }
is (b($es), b($ls)).join(','), 'List,List', 'an itemized Seq binds to @x as a List';
is (n(x => $es), n(x => $ls)).join(','), 'List,List', '…and to :@x';
is n(x => (1..*).map(* + 1)), 'List', 'a lazy Seq binds to :@x as a List';

# the dispatch cache's where pass sees the same List (a few calls, mixed)
class M {
    multi method m(@x where @x ~~ List:D) { 'list' }
    multi method m(*@)                   { 'fallback' }
}
my $m = M.new;
is (^4).map({ $m.m((1, 2).map(* + $_)) }).join(','), 'list,list,list,list', 'repeated calls: the cached dispatch agrees';

# not every Positional turns into a List: an Array stays an Array
multi r(@x where @x ~~ Array:D) { 'array' }; multi r(*@) { 'fallback' }
is r([1, 2]), 'array', 'an Array argument is untouched';
