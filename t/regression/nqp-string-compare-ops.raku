# The native string comparison ops: `cmp_s` answers -1/0/1 and the `is*_s`
# family answers 0/1. They did not exist, so Array::Sorted::Util — which walks
# its keys with them — could not run a line, and String::Color behind it could
# not either. `cmp_i`/`cmp_n` come with them.
use Test;
use nqp;
plan 8;

is nqp::cmp_s("a", "b"), -1,    'cmp_s orders ascending';
is nqp::cmp_s("b", "a"),  1,    '…and descending';
is nqp::cmp_s("x", "x"),  0,    '…and equal';
is nqp::cmp_i(3, 5),     -1,    'cmp_i on natives';

is nqp::iseq_s("a", "a"), 1,    'iseq_s';
is nqp::islt_s("a", "b"), 1,    'islt_s';
is nqp::isge_s("b", "a"), 1,    'isge_s';
is nqp::isgt_s("a", "b"), 0,    '…and a false one answers 0, not False';
