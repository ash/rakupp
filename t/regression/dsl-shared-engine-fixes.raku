# Regression, 2026-10-17: the engine gaps the test suites of DSL::Shared and
# DSL::English::IntegrationWorkflows ran into on the way to
# `rakupp install DSL::English::IntegrationWorkflows`, each answered as
# Rakudo 2026.09 answers it:
#   - a grammar that `does` a role still has Grammar as its parent, so it
#     passes a `Grammar :$grammar` parameter (ToWorkflowCode's dispatch)
#   - a token composed in from a role runs its code where the role was
#     written: `<?{ is-fuzzy-match(…) }>` sees the sub the role's FILE
#     imported, deferred `{ make … }` blocks included, while the grammar's own
#     rules still see the grammar's file
#   - an LTM `|` whose branch the NFA cannot model (a proto) in a grammar with
#     a `method ws` ranks a branch by how far it got before the method,
#     instead of discarding both
#   - an object passed as a rule argument, `<entity-name($obj, $class)>`,
#     reaches the callee as the object, also from `.parse(args => …)`
use Test;
plan 26;

# --- a grammar doing a role keeps its Grammar parent ----------------------
{
    role R { token x { a } }
    grammar EG does R { token TOP { <x> } }
    is EG.^mro.map(*.^name).join(' '), 'EG Grammar Match Capture Cool Any Mu', 'grammar does R: Grammar in the MRO';
    ok EG ~~ Grammar, 'grammar does R ~~ Grammar';
    ok EG.parse('a'), '…and still parses';
    class C { }
    grammar G2 does R is C { token TOP { <x> } }
    is G2.^mro.map(*.^name).join(' '), 'G2 C Any Mu', 'does R is C: the class is the parent';
    grammar G3 is C does R { token TOP { <x> } }
    is G3.^mro.map(*.^name).join(' '), 'G3 C Any Mu', 'is C does R: the class is the parent';
    role P { method parse($t, |c) { callsame } }
    grammar G5 does R does P { token TOP { <x> } }
    is G5.^mro.map(*.^name).join(' '), 'G5 Grammar Match Capture Cool Any Mu', 'two roles: Grammar in the MRO';
    is G5.subparse('ab').Str, 'a', 'a role parse override reaches Grammar.subparse';
    sub takes-grammar(Str $c, Grammar :$grammar!) { $grammar.parse($c).Bool }
    ok takes-grammar('a', grammar => EG), 'binds to a Grammar-typed parameter';
}

# --- a role's tokens run their code in the role's file scope --------------
my $dir = $*TMPDIR.add("rakupp-dsl-shared-fixes-$*PID");
mkdir $dir;
$dir.add('DsfFz.rakumod').spurt: q:to/END/;
    unit module DsfFz;
    sub is-fz(Str $a, Str $b) is export { $a.lc eq $b.lc ~ 'x' }
    END
$dir.add('DsfR.rakumod').spurt: q:to/END/;
    use DsfFz;
    role DsfR {
      token word { :i 'after' | (\w+) <?{ is-fz($0.Str, 'after') }> }
      method m($x) { is-fz($x, 'after') }
      token w2 { (\w+) { make is-fz($0.Str, 'after') } }
      regex w3 { (\w+) <?{ is-fz($0.Str, 'after') }> }
    }
    END
$dir.add('DsfG.rakumod').spurt: q:to/END/;
    use DsfR;
    grammar DsfG does DsfR { token TOP { <word> } }
    END

sub child($code) {
    my $p = run $*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err;
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).trim
}

is child('use DsfG; say so DsfG.parse($_) for <after afterx nope>'), "True\nTrue\nFalse",
   'grammar in a module, token from a role in another: the role file\'s import';
is child('use DsfR; class C does DsfR {}; say C.m("afterx")'), 'True', 'a role method (already worked)';
is child('use DsfR; grammar G does DsfR { token TOP { <w2> } }; say G.parse("afterx")<w2>.made'), 'True',
   'a deferred { make … } block in a role token';
is child('use DsfR; grammar G does DsfR { token TOP { <w3> } }; say so G.parse("afterx")'), 'True',
   'a role regex (backtracking)';
is child('use DsfR; sub helper($x) { $x eq "zz" }; my $lex = "yy"; '
       ~ 'grammar G does DsfR { token TOP { <word> | (\w+) <?{ helper(~$0) || ~$0 eq $lex }> } }; '
       ~ 'say so G.parse($_) for <after afterx zz yy nope>'),
   "True\nTrue\nTrue\nTrue\nFalse",
   'grammar in the main file: its own rules see the main file, the role\'s see the role\'s';
is child('use DsfR; my $*D = 7; grammar G does DsfR { token TOP { <word> <?{ $*D == 7 }> } }; say so G.parse("afterx")'),
   'True', 'the caller\'s dynamic variables still reach the rules';

sub rm-tree(IO::Path $p) { if $p.d { rm-tree($_) for $p.dir; rmdir $p } else { unlink $p } }
rm-tree($dir);   # (Rakudo leaves a .precomp directory there)

# --- LTM `|` with a proto branch, in a grammar with a ws method ------------
{
    grammar L1 { method ws() { callsame }; proto token sd {*}; token sd:sym<E> { :i 'set' };
                 rule TOP { <sd> | [i am nina] } }
    ok L1.parse('i am nina'), 'the branch past the method is a candidate';
    ok L1.parse('set'), 'the proto branch still wins its own input';
    grammar L2 { method ws() { callsame }; proto token sd {*}; token sd:sym<E> { :i 'set' };
                 rule TOP { [i am nina] | <sd> } }
    ok L2.parse('i am nina'), 'in either order';
    nok L1.parse('i am bob x'), 'and a real mismatch still fails';
    my $hits = 0;
    grammar L3 { method ws() { $hits++; callsame }; proto token sd {*}; token sd:sym<E> { :i 'set' };
                 rule TOP { <sd> | [i am nina] } }
    ok L3.parse('i am nina'), 'a counting ws method';
    ok $hits > 0, '…runs when the branch commits';
}

# --- an object as a rule argument ------------------------------------------
{
    class Res { method kn($c, $s) { $s eq 'ok' } }
    grammar A1 { rule TOP($obj, $class) { <en($obj, $class)> }
                 regex en(Res $r, $class) { (\w+) <?{ $r.kn($class, $0.Str) }> } }
    ok A1.parse('ok', args => (Res.new, 'X')), '.parse(args => …) object handed on to a subrule';
    nok A1.parse('no', args => (Res.new, 'X')), '…and its answer counts';
    grammar A2 { rule TOP($obj) { <en($obj, Whatever)> }
                 regex en($r, $class) { (\w+) <?{ $r.kn($class, $0.Str) }> } }
    ok A2.parse('ok', args => (Res.new,)), 'with a type object beside it';
    my $robj = Res.new;
    grammar A3 { token TOP { <en($robj)> }; regex en($r) { (\w+) <?{ $r.kn(1, $0.Str) }> } }
    ok A3.parse('ok'), 'a lexical object as the argument';
    grammar A4 { token TOP { <en(%(:a(1)))> }; token en(%h) { (\w+) <?{ %h<a> == 1 }> } }
    ok A4.parse('ok'), 'a Hash argument keeps its keys';
    grammar A5 { token TOP { <en('a b')> }; token en($s) { $s } }
    ok A5.parse('a b'), 'a Str argument still interpolates as text';
}
