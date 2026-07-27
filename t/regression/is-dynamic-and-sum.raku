# Regression: the `is dynamic` trait was parsed and thrown away.
#   * skipTraits swallowed every trait it did not special-case, so `dynamic` never
#     reached the AST and `.dynamic` could only ever answer from a `*` twigil.
#   * it is recorded per scope at declaration time now, and read back by walking
#     the scope chain — stopping at the scope that DECLARES the name, so an inner
#     plain `my $x` shadows an outer dynamic one.
#   * `$x.VAR.dynamic` asks the same question, so a `.VAR` in front is unwrapped.
#   * three declaration paths had to record it: the assignment form (`my $x is
#     dynamic = 3`), the bare form inside a block, and the MAINLINE bare form,
#     which is hoisted and never evaluated at all.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# the three declaration paths, at mainline scope
my %b is dynamic;
my $s is dynamic = 3;
my @a is dynamic;
check(%b.dynamic.gist,     'True', 'a bare mainline hash declaration');
check($s.VAR.dynamic.gist, 'True', 'a scalar with an initialiser');
check(@a.dynamic.gist,     'True', 'a bare array declaration');
# …and inside a block
{
    my $inner is dynamic;
    check($inner.VAR.dynamic.gist, 'True', 'a bare declaration in a block');
}

# the twigil form is unaffected
my $*d = 1;
check($*d.VAR.dynamic.gist, 'True', 'a `*` twigil is dynamic without the trait');
# and a plain variable is not
my $plain = 1;
check($plain.VAR.dynamic.gist, 'False', 'a plain scalar is not');
my %q;
check(%q.dynamic.gist, 'False', 'nor a plain hash');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
