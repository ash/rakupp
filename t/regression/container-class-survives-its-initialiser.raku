# `my %h is MyHash = …` declares the variable to BE a MyHash; the initialiser
# replaces its CONTENTS, not the container. It replaced the container instead,
# so the moment a declaration carried an initialiser `%h.^name` answered Hash
# and `%h ~~ MyHash` was False — AccountableBagHash's first assertion, with
# EERPG waiting behind it. A class with its own STORE was already safe; one that
# merely derives a built-in container was not.
use Test;
plan 6;

class HashKind    is Hash    { }
class BagKind     is BagHash { }
class SetKind     is SetHash { }

my %a is HashKind = a => 1;
is %a.^name, 'HashKind',        'a Hash-derived container keeps its type';
ok %a ~~ HashKind,              '…and type-checks as itself';
is %a<a>, 1,                    '…and holds what it was given';

my %b is BagKind = a => 42;
is %b.^name, 'BagKind',         'a BagHash-derived one too';

my %c is SetKind = a => True;
is %c.^name, 'SetKind',         '…and a SetHash-derived one';

my %d is HashKind;
is %d.^name, 'HashKind',        'declaring without an initialiser is unchanged';
