# `is implementation-detail` on a type is a TRAIT, not a superclass. The class
# declaration read any bare `is NAME` as inheritance — right for `class A is B`
# — and offered an unknown name to a user `trait_mod:<is>` before complaining,
# so a class carrying Rakudo's one no-argument type trait died with "cannot
# inherit from 'implementation-detail' because it is unknown". Every template
# Cro::WebApp compiles is marked that way.
use Test;
plan 3;

class Marked is implementation-detail {
    has $.n = 7;
    method answer { 42 }
}
is Marked.new.answer, 42,               'a class may carry the trait and still work';
is Marked.^name, 'Marked',              '…and it is not renamed by it';
is Marked.new(n => 9).n, 9,             '…and constructs as any class does';
