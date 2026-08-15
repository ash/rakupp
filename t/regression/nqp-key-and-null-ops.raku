# Regression: the `use nqp` subset gained the READ side of its hash surface and
# the null/unbox leaves. We had `nqp::hash` and `nqp::bindkey` but no way to read
# back — `nqp::atkey` was an "Undefined routine", so a module that built a table
# with nqp could never look anything up in it (HTML::Entity::Fast does exactly
# that). `nqp::unbox_s` and friends are how a module gets at a Str's guts
# (Path::Finder), and `nqp::isnull` tests the null those ops can return.
#
# Also the `nqp::stat` field selectors as MoarVM numbers them, so
# `nqp::const::STAT_PLATFORM_INODE` resolves — Path::Finder asks for the inode
# to detect directory loops. (`nqp::stat` itself is still missing; the constants
# are just integers and cost nothing.)
# Contract: exit 0 + last line PASS.
use nqp;
my @fail;
sub ok($desc, $got, $want) { @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my $h := nqp::hash('a', 1, 'b', 2);
ok('atkey',            nqp::atkey($h, 'a'), 1);
ok('atkey missing',    nqp::isnull(nqp::atkey($h, 'zz')), 1);
ok('existskey yes',    nqp::existskey($h, 'b'), 1);
ok('existskey no',     nqp::existskey($h, 'zz'), 0);
nqp::bindkey($h, 'c', 3);
ok('bindkey then atkey', nqp::atkey($h, 'c'), 3);
nqp::deletekey($h, 'a');
ok('deletekey',        nqp::existskey($h, 'a'), 0);
ok('the rest survive', nqp::atkey($h, 'b'), 2);

ok('unbox_s',          nqp::unbox_s('hello'), 'hello');
ok('unbox_i',          nqp::unbox_i(42), 42);

# VM-level null is NOT Raku's undefined: Rakudo answers 0 for Nil and Any alike.
# What must be null is what a missing key hands back, which the case above pins.
ok('isnull on Any',    nqp::isnull(Any), 0);
ok('isnull on a value', nqp::isnull('x'), 0);
ok('isnull on 0',      nqp::isnull(0), 0);

# the stat selectors are plain integers, and distinct
# …and the PLATFORM_ ones are NEGATIVE, which is the sort of thing to read off
# the reference implementation rather than guess: the first draft said 13 and 14.
my @stat = nqp::const::STAT_EXISTS, nqp::const::STAT_ISDIR,
           nqp::const::STAT_PLATFORM_DEV, nqp::const::STAT_PLATFORM_INODE;
ok('stat selectors', @stat, [0, 2, -1, -2]);

# the ops that were already there keep working
ok('elems',  nqp::elems(nqp::list(1, 2, 3)), 3);
ok('atpos',  nqp::atpos(nqp::list('x', 'y'), 1), 'y');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
