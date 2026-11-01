# Regression: the NQP ops Red uses beside the Raku it is written in (issue
# #77, FCO's nqp::where / nqp::newexception item).
#
# Red's X::Red::Transaction::AsyncResult raises a RESUMABLE warning by hand:
#
#   my $exception := nqp::newexception();
#   nqp::setmessage($exception, self.message);
#   nqp::setextype($exception, nqp::const::CONTROL_WARN);
#   nqp::setpayload($exception, self);
#   nqp::throw($exception);
#
# and its suite counts them in a `CONTROL { when … { .resume } }`; its query
# plans compare shared ASTs with `nqp::where`. None of those ops, nor the
# CONTROL_ constants, were there.

use nqp;

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck (nqp::const::CONTROL_ANY, nqp::const::CONTROL_WARN, nqp::const::CONTROL_DONE), (2, 256, 32768), 'the CONTROL_ constants';

my $e := nqp::newexception();
ck $e.^name, 'BOOTException', 'newexception';
ck nqp::setmessage($e, 'm'), 'm', 'setmessage answers the message';
ck nqp::getmessage($e), 'm', '…and getmessage reads it';
ck (nqp::setpayload($e, 42), nqp::getpayload($e)), (42, 42), 'the payload';
ck (nqp::setextype($e, 256), nqp::getextype($e)), (256, 256), 'the category';

class AsyncResult is CX::Warn {
    method message { 'pending Promise' }
    method warn {
        my $exception := nqp::newexception();
        nqp::setmessage($exception, self.message);
        nqp::setextype($exception, nqp::const::CONTROL_WARN);
        nqp::setpayload($exception, self);
        nqp::throw($exception);
    }
}
my $warnings = 0;
my @after;
{
    CONTROL { when AsyncResult { $warnings++; .resume } }
    AsyncResult.new.warn;
    @after.push: 'first';
    AsyncResult.new.warn;
    @after.push: 'second';
}
ck $warnings, 2, 'a CONTROL_WARN reaches CONTROL with its payload';
ck @after, ['first', 'second'], '…and .resume carries on after each';

my $plain := nqp::newexception();
nqp::setmessage($plain, 'boom');
my $caught = '';
try { nqp::throw($plain); CATCH { default { $caught = .message } } }
ck $caught, 'boom', 'any other category is thrown, its message the error';

my $a = [1];
my $b = $a;
ck nqp::where($a) == nqp::where($b), True, 'where: one object, one answer';
my $c = [1];
ck nqp::where($a) == nqp::where($c), False, 'two equal ones differ';
ck nqp::where(Int) == nqp::where(Int), True, 'a type object is itself';
ck nqp::where($a) ~~ Int, True, 'it is an Int';

say 'PASS';
