# Regression: parameterized roles with VALUE parameters — `role R[$x]` / `role
# R[%h]` / `role R[Bool :$opt]` — must bind the composition's `does R[args]`
# arguments and make them visible in the role body (methods and submethods).
# Previously the args were parsed and discarded, so the body saw "Variable '$x'
# is not declared". This is the shape Cro::Policy::Timeout[%phase-defaults] needs.
# Contract: exit 0 + last line PASS.
my @fail;

# positional value param, used in a method
role V[$x] { method val { $x } }
class CV does V[42] { }
@fail.push("value ({CV.new.val})") unless CV.new.val == 42;

# two positional params
role Two[$a, $b] { method sum { $a + $b } }
class CT does Two[3, 4] { }
@fail.push('two-pos') unless CT.new.sum == 7;

# named / optional param bound from `does R[:opt]`
role N[Bool :$opt] { method o { $opt } }
class CN does N[:opt] { }
@fail.push("named ({CN.new.o})") unless CN.new.o === True;

# hash value param used in a plain method
role HM[%d] { method keys-of { %d.keys.sort.join(',') } }
class CHM does HM[%(a => 1, b => 2)] { }
@fail.push("hash-method ({CHM.new.keys-of})") unless CHM.new.keys-of eq 'a,b';

# hash value param used inside submethod BUILD (the Cro::Policy::Timeout shape)
role HB[%pd] {
    has %.phases;
    submethod BUILD {
        for %pd.kv -> $k, $v { %!phases{$k} = $v }
    }
}
class CHB does HB[%(x => 10, y => 20)] { }
@fail.push("hash-build ({CHB.new.phases.raku})")
    unless CHB.new.phases<x> == 10 && CHB.new.phases<y> == 20;

# two parameterized roles composed on one class keep distinct params
role PA[$p] { method a { $p } }
role PB[$q] { method b { $q } }
class CAB does PA[1] does PB[2] { }
@fail.push('two-roles') unless CAB.new.a == 1 && CAB.new.b == 2;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
