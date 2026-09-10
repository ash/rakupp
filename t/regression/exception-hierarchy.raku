# Regression: the X:: exception types are a HIERARCHY, and code reads it.
# Every X:: name used to be a bare type object here with no parents and no
# roles, so `X::TypeCheck::Binding::Parameter ~~ X::TypeCheck::Binding` was
# False and `CATCH { when X::TypeCheck {…} }` never fired — only an exact
# class-name match worked. All 383 ancestry/role relations Rakudo declares
# answered False; the table in src/exception_ancestry_gen.cpp is the fix.
#
# The second half is the leaf NAME: a failure binding to a signature parameter
# is X::TypeCheck::Binding::Parameter, not the plain X::TypeCheck::Binding that
# `:=` to a typed variable throws. Both were spelled the same way here, which is
# 466 cells of rakugrid's signatures family.
#
# Contract: exit 0 + last line PASS.
my @fail;

# ---- 1. the type objects conform ------------------------------------------
@fail.push('leaf~~base')     unless X::TypeCheck::Binding::Parameter ~~ X::TypeCheck::Binding;
@fail.push('base~~root')     unless X::TypeCheck::Binding ~~ X::TypeCheck;
@fail.push('leaf~~root')     unless X::TypeCheck::Binding::Parameter ~~ X::TypeCheck;
@fail.push('leaf~~Exception')unless X::TypeCheck::Binding::Parameter ~~ Exception;
# …and a role, which is how the compile-time family is marked
@fail.push('does-Comp')      unless X::Undeclared::Symbols ~~ X::Comp;
@fail.push('does-Syntax')    unless X::Syntax::Confused ~~ X::Syntax;
@fail.push('does-IO')        unless X::IO::Rename ~~ X::IO;

# A name OUTSIDE the table is still an Exception — a user's own X:: class, or
# one the engine invents at a throw site, must not fall out of the tree. Named
# through ::() because Rakudo has no such type and refuses the literal at
# compile time; this file runs on BOTH engines on purpose, so that every
# expectation in it is one Rakudo itself confirms.
my $unknown = ::('X::No::Such::Thing::Here');
@fail.push('unknown-is-Exception') if $unknown.^name ne 'Failure' && !($unknown ~~ Exception);

# …and the tree does not over-claim: unrelated leaves stay unrelated.
@fail.push('over-claim')     if X::TypeCheck::Binding ~~ X::TypeCheck::Return;
@fail.push('over-claim-2')   if X::Numeric::DivideByZero ~~ X::TypeCheck;

# ---- 2. .^mro / .isa / .does split the classes from the roles --------------
@fail.push('mro') unless X::TypeCheck::Binding::Parameter.^mro.map(*.^name).join(',')
    eq 'X::TypeCheck::Binding::Parameter,X::TypeCheck::Binding,X::TypeCheck,Exception,Any,Mu';
@fail.push('isa-class') unless X::TypeCheck::Binding::Parameter.isa(X::TypeCheck);
# a ROLE is done, not inherited: .isa says no, .does says yes (as in Rakudo)
@fail.push('isa-role')  if     X::Undeclared::Symbols.isa(X::Comp);
@fail.push('does-role') unless X::Undeclared::Symbols.does(X::Comp);

# ---- 3. a THROWN exception answers the same way ---------------------------
sub thrown(&code) {
    my $ex;
    { code(); CATCH { default { $ex = $_ } } }
    $ex
}
my $s = "a";                        # keeps the bad call out of static reach
my $b = thrown { sub f(Int $a) { $a }; f($s) };
@fail.push('no-throw')       unless $b;
@fail.push('thrown-leaf')    unless $b && $b.^name eq 'X::TypeCheck::Binding::Parameter';
@fail.push('thrown~~base')   unless $b && $b ~~ X::TypeCheck::Binding;
@fail.push('thrown~~root')   unless $b && $b ~~ X::TypeCheck;

# `when` on a SUPERTYPE is the spelling real programs use
my $caught = 'none';
{ sub f(Int $a) { $a }; f($s); CATCH { when X::TypeCheck { $caught = 'supertype' }
                                       default           { $caught = .^name } } }
@fail.push("catch-when ($caught)") unless $caught eq 'supertype';

# ---- 4. …and the leaf name is the PARAMETER one everywhere it should be ----
# every one of these is a binding to a parameter, so every one is ::Parameter
my @param-cases =
    'named',      { sub f(Int :$a) { $a }; f(a => $s) },
    'method',     { class C { method m(Int $a) { $a } }; C.m($s) },
    'block',      { my $blk = -> Int $a { $a }; $blk($s) },
    'constraint', { my $n = -1; sub f($a where * > 0) { $a }; f($n) };
for @param-cases -> $tag, &c {
    my $e = thrown(&c);
    @fail.push("param-$tag ({$e ?? $e.^name !! 'no throw'})")
        unless $e && $e.^name eq 'X::TypeCheck::Binding::Parameter';
}
# …while `:=` to a typed VARIABLE keeps the plain binding class (Rakudo-verified)
my $v = thrown { my Int $x := $s };
@fail.push("var-bind ({$v ?? $v.^name !! 'no throw'})")
    if $v && $v.^name ne 'X::TypeCheck::Binding';

# ---- 5. the classes Raku does not have are caught by what Rakudo throws -----
# rakupp names a few situations Raku has no type for. Each keeps its own name
# AND answers to what Rakudo throws for the same code, so `when X::AdHoc` —
# which is what a program written against Rakudo contains — fires on both.
# The parent is not decoration: it is the class Rakudo was probed for.
sub caught-as(&code, $want) {
    my $branch = 'none';
    {
        code();
        CATCH {
            when $want { $branch = 'matched' }
            default    { $branch = .^name }
        }
    }
    $branch
}
my $one = 1;
@fail.push('arity~~AdHoc')
    unless caught-as({ sub f($a, $b) { $a }; my &g = &f; g($one) }, X::AdHoc) eq 'matched';
@fail.push('reqnamed~~AdHoc')
    unless caught-as({ sub f($a?, :$b!) { $a }; my &g = &f; g() }, X::AdHoc) eq 'matched';
# …and here they keep the name that says more than X::AdHoc does. Rakudo throws
# the bare X::AdHoc, so this half is the one thing in the file that is ours
# alone — the `when` above is what has to agree, and does.
if $*RAKU.compiler.name eq 'rakupp' {
    my $a = thrown({ sub f($a, $b) { $a }; my &g = &f; g($one) });
    @fail.push("arity-keeps-name ({$a ?? $a.^name !! 'no throw'})")
        unless $a && $a.^name eq 'X::Signature::ArityMismatch';
}

# A bad temporal string is X::Temporal::InvalidFormat — Rakudo's own class, not
# the X::DateTime::InvalidFormat one throw site here used to invent. Both the
# non-ISO form and a malformed timezone offset reach it.
for '2012/04', 'not-a-datetime', '2012-04-01T12:00:00+1' -> $bad {
    my $e = thrown({ DateTime.new($bad) });
    @fail.push("temporal ($bad -> {$e ?? $e.^name !! 'no throw'})")
        unless $e && $e.^name eq 'X::Temporal::InvalidFormat';
}
# …which does the X::Temporal role, so `when X::Temporal` catches the family
@fail.push('temporal~~role')
    unless caught-as({ DateTime.new('not-a-datetime') }, X::Temporal) eq 'matched';

die "FAILED: {@fail.join(', ')}" if @fail;
say 'PASS';
