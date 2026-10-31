# Regression: `Lock.condition` is a Lock::ConditionVariable, and that is a
# type a program can name (issue #77, FCO's Lock.condition item).
#
# The condition was a built-in kind called "LockCondition", and
# `Lock::ConditionVariable` was no symbol at all: Red's Transaction::Guard
# declares `has Lock::ConditionVariable $!available` and died "Could not
# find symbol '&ConditionVariable' in 'GLOBAL::Lock'".

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $lock = Lock.new;
my $cond = $lock.condition;
ck $cond.^name, 'Lock::ConditionVariable', 'its name';
ck $cond ~~ Lock::ConditionVariable, True, 'it smartmatches the type';
ck Lock::ConditionVariable.^name, 'Lock::ConditionVariable', 'the type object names itself';

sub takes(Lock::ConditionVariable $c) { 'bound' }
ck takes($cond), 'bound', 'a parameter of the type binds it';

class Guard {
    has Lock $!lock = Lock.new;
    has Lock::ConditionVariable $!available = $!lock.condition;
    has Bool $!free = False;
    method release { $!lock.protect: { $!free = True; $!available.signal } }
    method wait-free { $!lock.protect: { $!available.wait({ $!free }) }; $!free }
}
my $g = Guard.new;
my $t = start { $g.release };
ck $g.wait-free, True, 'an attribute of the type waits and is signalled';
await $t;

say 'PASS';
