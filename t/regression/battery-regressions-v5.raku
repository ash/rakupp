# Regression: eight faults the module battery found at the v5.0.0 gates, once
# its reference engine really was Rakudo. Each one broke a distribution that
# passed its own suite under v4.0.1.
#
#   URI          an `our subset` attribute default refused at declaration
#   IO::Glob,    `$*SPEC ~~ IO::Spec` was False, and the constructor now checks
#   Config       attribute types
#   XML          a coercion parameter refused its own target type, and took an
#                argument that was neither its target nor its source
#   Hash::Merge  `self = …` in a Hash method refused, where it is a store
#   Cro::Core    a named subrule inside a quantified capture came back a List
#   JSON::Tiny   a module loaded in a block lost sight of what it loaded itself
#   Cro::Core    a tag built by hand (`package EXPORT::tag { our &f = … }`)
#                imported nothing
# …and one the conformance gate found in raku.online's own report tool:
#   `for %h.kv -> $k, @rows` bound @rows to the item container, so `for @rows`
#   iterated once, over the whole array
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use lib $?FILE.IO.parent.add('lib').Str;
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

# URI
use RakuppSubsetDefault;
ck(RakuppSubsetDefault.new.tag, '', 'an our-subset attribute default');

# IO::Glob, Config
ck($*SPEC ~~ IO::Spec, True, '$*SPEC is an IO::Spec');
class WithSpec { has IO::Spec $.spec }
ck(WithSpec.new(spec => $*SPEC).spec === $*SPEC, True, 'an IO::Spec attribute takes $*SPEC');

# XML
sub coerce-to-int(Int(Str) $x) { $x }
ck(coerce-to-int(5), 5, 'a coercion takes its target type');
ck(coerce-to-int('7'), 7, '…and coerces its source type');
multi sub which-src(IO::Path(Str) $src) { 'path' }
multi sub which-src(IO::Handle $src)    { 'handle' }
ck(which-src($*PROGRAM), 'path', 'an IO::Path goes to the coercion candidate');
ck(which-src($*PROGRAM.Str), 'path', '…and so does a Str');
my $h = $*PROGRAM.open;
ck(which-src($h), 'handle', 'an IO::Handle goes to its own candidate');
$h.close;

# Hash::Merge
use MONKEY-TYPING;
augment class Hash { method merged-with(%more) { self = %(|self, |%more); self } }
my %base = a => 1;
%base.merged-with({ b => 2 });
ck(%base.keys.sort.List, <a b>, 'self = … in a Hash method stores');

# Cro::Core
grammar Frag {
    token TOP   { ( <chars> | '/' )* }
    token chars { <[a..z]>+ }
}
my $m = Frag.parse('ab/cd');
ck(($m[0][0]<chars> ~~ Match) && $m[0][0]<chars>.from, 0, 'a subrule inside a quantified capture is one Match');
ck($m[0][1]<chars>.defined, False, '…and absent where that occurrence took the other branch');

# JSON::Tiny
{
    use RakuppDepUser;
    ck(dep-greet(), 'helper', 'a module loaded in a block sees what it loaded');
}

# Cro::Core, Cro::HTTP
{
    use RakuppTagTop :tag-dp;
    ck(tag-dp('x'), 'dp:x', 'a hand-built EXPORT tag imports its routine');
}

# raku.online's divergences.raku
{
    my %m = a => [<x differ>, <y agree>];
    my @seen;
    for %m.kv -> $k, @rows { for @rows -> @r { @seen.push: @r[1] } }
    ck(@seen.List, <differ agree>, 'an @ loop parameter binds the array, not its item');
    my $x = [1, 2, 3];
    my @got;
    for ($x,) -> @r { @got.push: @r.elems }
    ck(@got.List, (3,), '…from an itemized array too');
}

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
