# Regression: the second batch from 3.log. Each line was checked under
# Rakudo 2026.08 as well, except the lookahead-of-a-composed-class lines:
# those are Roast's S05-metasyntax/charset.t expectations, which 2026.08
# predates.
#   - `use v6c` (no dot) is 6.c, as `use v6.c` is (6.c A03 01-misc.t)
#   - `$CALLER::_` writes the caller's topic, not the routine's own
#   - a C99 hex float literal (`0x1.8p1`) is a Num (S02-literals/numeric.t)
#   - `<?[a] - [b]>` is a zero-width lookahead of the whole composed class
#   - the LTM ranker knows non-ASCII members of \w and <+alnum>
#     (longest-alternative.t)
#   - a regex code block does not see a `$0` left over from an earlier match
#     (caps.t)
#   - `** { … }` bounds take a `%` separator (regex.t)
#   - `.match(…, :exhaustive)` is every match, and `:nth` picks one (regex.t)
#   - a regex code block does not leave a failing test at "line 1"
#   - `return` inside a routine-level `when` / `default` returns from that
#     routine only, not from its caller too (integration/precompiled.t)
#   - a module loaded at run time (`require`, `$*REPO.need`) sees the
#     caller's dynamic variables, and so do the modules it loads; a
#     compile-time `use` does not
#   - a role group used before its declarations are reached is built whole,
#     even when a sibling scope has a role of the same name (qualified.t)
#   - `role R2[::T] does R1[::T]` makes R2[Num] do R1[Num], in dispatch and
#     in `.^roles`
#   - `self.R::m` names the parameterization the CALLING code's type composed
#   - a role group spans compilation units, each candidate keeps its own
#     language revision, and `R.new` / `R[Str]` pun the candidate that fits
#     (S14-roles/versioning.t); a group's plain method call is the default
#     candidate's
#   - a 6.e role's BUILD/TWEAK run as role constructors in a 6.c class too
# Contract: exit 0 + last line PASS.
my @fail;

# the dot in a version pragma is optional
for <c d> -> $l {
    my $p = run $*EXECUTABLE, '-e', "use v6$l; print \$*RAKU.version", :out, :err;
    my $o = $p.out.slurp(:close);
    @fail.push("use v6$l is 6.$l, got {$o.raku}") unless $o eq "6.$l";
}

# $CALLER::_ is the caller's $_, for reading and for writing — under 6.c,
# where $_ is dynamic; from 6.d it is refused (X::Caller::NotDynamic)
{
    my $code = q:to/CODE/;
        use v6.c;
        my sub w { $CALLER::_ = 7 }
        $_ = 42;
        w();
        print $_;
        my sub inc { $CALLER::_++ }
        inc();
        print " $_";
        my sub r { $_ = 5; $CALLER::_ }
        print " ", r();
        CODE
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    my $o = $p.out.slurp(:close);
    @fail.push("6.c \$CALLER::_ writes and reads the caller's, got {$o.raku}") unless $o eq '7 8 8';
    my $q = run $*EXECUTABLE, '-e', 'use v6.d; sub r { $CALLER::_ }; $_ = 1; r()', :out, :err;
    @fail.push('6.d refuses $CALLER::_') unless $q.err.slurp(:close).contains('not declared as dynamic');
}

# hex float literals (EVAL'd, so a build without them still runs the rest)
{
    my $h = try EVAL '(0x1.8p1, 0xAp-2, 0x1_0p0)';
    @fail.push("0x1.8p1 0xAp-2 0x1_0p0, got {$h.raku}") unless $h eqv (3e0, 2.5e0, 16e0);
}

# a lookahead of a composed class is zero-width
@fail.push('<?[a] - [b]> is zero-width') unless ('aa' ~~ /<?[a] - [b]> ./).Str eq 'a';
@fail.push('<![a] - [b]> is zero-width') unless ('bb' ~~ /<![a] - [b]> ./).Str eq 'b';
@fail.push(':i <?[\s a]> is zero-width') unless ('Aa' ~~ /:i <?[\s a]> ./).Str eq 'A';

# the longest-token ranker sees non-ASCII class members
@fail.push('LTM: <+alnum> .+ is longest over "þ,"')
    unless ('þ,' ~~ /^^ [ <+alnum> | <+alnum> .+ ] $$/).Str eq 'þ,';
@fail.push('LTM: \w|x takes "þ"') unless ('þ' ~~ /\w|x/).Str eq 'þ';

# no stale $0 inside a code block
{
    'zz' ~~ /(z)/;
    my @e;
    'ab' ~~ /a { @e.push($0.raku) } b/;
    @fail.push("stale \$0 in a code block, got {@e.raku}") unless @e eqv ['Nil'];
}

# `** { … }` with a separator
@fail.push('**? {1..3} % ","') unless ("a,a,a,c" ~~ /^ (a **? {1..3} % ",") ",c"/)[0].Str eq 'a,a,a';
@fail.push('** {2} % "," refuses "aa%,"') if "aa%," ~~ /^ a ** {2} % "," $/;

# .match with :exhaustive / :nth
@fail.push(":exhaustive, got {"abcd".match(/. ** 2..3/, :exhaustive)>>.Str.raku}")
    unless "abcd".match(/. ** 2..3/, :exhaustive)>>.Str eqv <abc ab bcd bc cd>;
@fail.push(':ex :nth(2)') unless "abcd".match(/. ** 2..3/, :ex, :nth(2)).Str eq 'ab';

# a failing test after a regex code block names its own line
{
    my $code = "use Test;\n\n\nis (q[a] ~~ /a \{ 1 \}/).Str, q[b], q[x];\n";
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    @fail.push("failure line after a code block, got {$e.raku}") unless $e.contains('line 4');
}

# `return` in a routine-level when/default is that routine's return
{
    sub w1($_) { when Int { return "R1" }; "not" }
    sub o1 { my $r = w1(1); "after $r" }
    @fail.push("when + return, got {o1().raku}") unless o1() eq 'after R1';
    sub w2($_) { default { return } }
    sub o2 { my $r = w2(1); "after {$r.raku}" }
    @fail.push("default + bare return, got {o2().raku}") unless o2() eq 'after Any';
    class WK { method m($_) { when Str { return "M" }; "not" } }
    sub o3 { my $r = WK.m("s"); "after $r" }
    @fail.push("method when + return, got {o3().raku}") unless o3() eq 'after M';
    multi sub w4($_) { when Int { CATCH { default { } }; return "MM" } }
    sub o4 { my $r = w4(3); "after $r" }
    @fail.push("multi when + CATCH + return, got {o4().raku}") unless o4() eq 'after MM';
    my @o; for 1..4 { when 2 { next }; when 3 { last }; @o.push($_) }
    @fail.push("next/last in a loop-body when, got {@o.raku}") unless @o eqv [1];
}

# a run-time load sees the caller's dynamic variables
{
    my $dir = $*TMPDIR.add("rtdyn-{$*PID}");
    $dir.mkdir;
    # (no precompilation: Rakudo's precompiling child shares this stdout)
    $dir.add('RtDynC.rakumod').spurt: "no precompilation;\nuse RtDynD;\nunit module RtDynC;\nprint 'C=', \$*dq // 'NOTFOUND', ' ';\n";
    $dir.add('RtDynD.rakumod').spurt: "no precompilation;\nunit module RtDynD;\nprint 'D=', \$*dq // 'NOTFOUND', ' ';\n";
    $dir.add('RtDynE.rakumod').spurt: "no precompilation;\nunit module RtDynE;\nprint 'E=', \$*dq // 'NOTFOUND', ' ';\n";
    $dir.add('RtDynB.rakumod').spurt: "no precompilation;\nunit module RtDynB;\nprint 'B=', \$*dq // 'NOTFOUND', ' ';\n";
    my $code = q:to/CODE/;
        sub go { my $*dq = "q1"; require RtDynC; }
        go();
        sub go2 { my $*dq = "q2"; $*REPO.need(CompUnit::DependencySpecification.new(:short-name<RtDynE>)); }
        go2();
        sub go3 { my $*dq = "q3"; use RtDynB; }
        go3();
        CODE
    my $p = run $*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err;
    my $o = $p.out.slurp(:close); $p.err.slurp(:close);
    # (Rakudo loads RtDynB while the program compiles, so B comes first there)
    @fail.push("run-time loads see the caller's dynamics, got {$o.raku}")
        unless $o.words.sort eqv <B=NOTFOUND C=q1 D=q1 E=q2>.sort;
    sub nuke(IO::Path $p) {
        if $p.d { nuke($_) for $p.dir; try $p.rmdir }
        else { try $p.unlink }
    }
    nuke($dir);
}

# early materialization builds the whole role group; a sibling's R1 is not it
{ my role PR1 { method who { "sibling" } } }
{
    my class PC1 { ... }
    my $got = try PC1.new.of-type;
    my role PR1[::T] { method of-type { "PR1[" ~ T.^name ~ "]" } }
    my role PR1 { method of-type { "PR1" } }
    my class PC1 does PR1[Int] { }
    @fail.push("early role group, got {$got.raku}") unless $got eq 'PR1[Int]';
}

# roles done through a parameter are concretized with the parameter
{
    my role QR1[::T] { method t1 { "QR1[" ~ T.^name ~ "]" } }
    my role QR2[::T] does QR1[::T] { method t2 { self.QR1::t1 } }
    my class QG does QR2[Num] { }
    @fail.push("R2[Num] does R1[Num], got {QG.new.t2}") unless QG.new.t2 eq 'QR1[Num]';
    @fail.push("QG ~~ QR1[Num]") unless QG ~~ QR1[Num];
    @fail.push(".^roles, got {QG.^roles.map(*.^name).raku}")
        unless QG.^roles.map(*.^name).list eqv ("QR2[Num]", "QR1[Num]");
}

# self.R::m resolves from the calling code's type, not the invocant's
{
    my role SR1[::T] { method of-type { "SR1[" ~ T.^name ~ "]" } }
    my class SC1 does SR1[Int] { method mine { self.SR1::of-type } }
    my class SC2 is SC1 does SR1[Str] { }
    @fail.push("caller's concretization, got {SC2.new.mine}") unless SC2.new.mine eq 'SR1[Int]';
}

# role groups across units and revisions; 6.e role constructors
{
    my $dir = $*TMPDIR.add("rolever-{$*PID}");
    $dir.mkdir;
    $dir.add('RvA.rakumod').spurt: "use v6.c;\nno precompilation;\nrole RvRole is export \{ }\n";
    $dir.add('RvB.rakumod').spurt: "use v6.d;\nno precompilation;\nuse RvA;\nrole RvRole[::T] is export \{ }\n";
    $dir.add('RvE.rakumod').spurt: q:to/MOD/;
        use v6.e.PREVIEW;
        no precompilation;
        role RvE[@s] is export {
            submethod BUILD { @s.push: "E.BUILD" }
            submethod TWEAK { @s.push: "E.TWEAK" }
        }
        MOD
    my $p = run $*EXECUTABLE, '-I', ~$dir, '-e',
        'use RvA; use RvB; print RvRole.^candidates.map(~*.^language-revision).join(","), " ", RvRole.new.^language-revision, " ", RvRole[Str].new.^language-revision',
        :out, :err;
    my $o = $p.out.slurp(:close); $p.err.slurp(:close);
    @fail.push("role group across units, got {$o.raku}") unless $o eq 'c,d c d';
    my $q = run $*EXECUTABLE, '-I', ~$dir, '-e', q:to/CODE/, :out, :err;
        use v6.c;
        use RvE;
        my @s;
        role RvC[@s] { submethod BUILD { @s.push: "C.BUILD" }; submethod TWEAK { @s.push: "C.TWEAK" } }
        class K does RvE[@s] does RvC[@s] { }
        K.new;
        print @s.join(" ");
        CODE
    $o = $q.out.slurp(:close); $q.err.slurp(:close);
    @fail.push("6.e role constructors in a 6.c class, got {$o.raku}") unless $o eq 'E.BUILD C.BUILD E.TWEAK C.TWEAK';
    sub nuke2(IO::Path $p) {
        if $p.d { nuke2($_) for $p.dir; try $p.rmdir }
        else { try $p.unlink }
    }
    nuke2($dir);
}
{
    my role GW { method who { "plain" } }
    my role GW[::T] { method who { "one" } }
    my role GW[::S, ::T] { method who { "two" } }
    my @w = GW.new.who, GW[Int].new.who, GW[Int, Str].new.who, GW.who;
    @fail.push("role group candidates, got {@w.raku}") unless @w eqv [<plain one two plain>];
    @fail.push("a pun's .^roles names it once, got {GW[Int].new.^roles.map(*.^name).raku}")
        unless GW[Int].new.^roles.map(*.^name).list eqv ('GW[Int]',);
}

if @fail { say "FAILED:"; .say for @fail; say "FAIL"; exit 1 }
say "PASS";
