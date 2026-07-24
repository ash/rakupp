# Regression: `has T $!x .= new` attribute initializers were dropped by the parser
# (only `= EXPR` defaults were captured), so `$!x` stayed the bare type object.
# `.= new` desugars to `$!x = $!x.new`, with `$!x` seeded to T's type object at
# construction. Cro::HTTP::Client's `has Lock::Async $!lock .= new` needs this.
# Contract: exit 0 + last line PASS.
my @fail;

# user class, private attr
class W { has $.n = 0; method bump { $!n++ } }
class CP { has W $!w .= new; method go { $!w.bump; $!w.bump; $!w.n } }
@fail.push('user-priv') unless CP.new.go == 2;
@fail.push('user-priv-defined') unless (class :: { has W $!w .= new; method d { $!w.defined } }).new.d;

# public attr
class CPub { has W $.w .= new; }
@fail.push('user-pub') unless CPub.new.w.defined && CPub.new.w ~~ W;

# with constructor args
class CArgs { has W $!w .= new(n => 5); method n { $!w.n } }
@fail.push('with-args') unless CArgs.new.n == 5;

# built-in types: Lock::Async (keeps its own identity + shares Lock methods)
class CL { has Lock::Async $!l .= new; method go { $!l.protect({ 42 }) } }
@fail.push('lock-async') unless CL.new.go == 42;
@fail.push('lock-async-name') unless Lock::Async.new.^name eq 'Lock::Async';

# Promise as a typed attr
class CPr { has Promise $!p .= new; method d { $!p.defined } }
@fail.push('promise') unless CPr.new.d;

# plain `= EXPR` defaults must be unaffected
class CD { has $.x = 5; has Int $.y; has @.a = (1, 2, 3); }
@fail.push('plain-default') unless CD.new.x == 5;
@fail.push('typed-no-default') unless CD.new.y.^name eq 'Int' && !CD.new.y.defined;
@fail.push('array-default') unless CD.new.a eqv [1, 2, 3];

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
