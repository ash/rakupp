# Regression: run()'s :env and :cwd named arguments were parsed as ordinary
# pairs and SILENTLY IGNORED — the child always inherited the parent
# environment and directory. That broke any harness that isolates children via
# `run(..., :env(%(%*ENV, RAKULIB => ...)))`: the spec site's module-example
# verifier resolved modules from the machine's installed inventory instead of
# the pinned dists it was told to use, and only mismatch patterns exposed it.
# Now :env REPLACES the child environment (Rakudo semantics; the child swaps
# `environ` pre-exec) and :cwd sets its directory — on BOTH spawn paths: the
# direct one and the deferred `:in` one (which stashes them on the Proc).
# Contract: exit 0 + last line PASS.
my @fail;

# :env on the direct path — the var must arrive, and a var EXCLUDED from the
# hash must NOT leak in (it is a replacement, not an overlay)
my %env = %*ENV;
%env<RAKUPP_RE_TEST> = 'delivered';
%env<PATH> = %*ENV<PATH>;
my $p = run('sh', '-c', 'echo "got=$RAKUPP_RE_TEST"', :out, :env(%env));
@fail.push('env not delivered') unless $p.out.slurp(:close).trim eq 'got=delivered';

my %bare = PATH => %*ENV<PATH>;   # deliberately WITHOUT HOME
my $p2 = run('sh', '-c', 'echo "home=${HOME:-unset}"', :out, :env(%bare));
@fail.push('env not a replacement') unless $p2.out.slurp(:close).trim eq 'home=unset';

# :env on the deferred (:in) path — the same delivery through .in.print
my $p3 = run('sh', '-c', 'read line; echo "$line-$RAKUPP_RE_TEST"', :in, :out, :env(%env));
$p3.in.print("hi\n");
@fail.push('deferred env') unless $p3.out.slurp(:close).trim eq 'hi-delivered';

# :cwd on both paths
my $p4 = run('pwd', :out, :cwd('/tmp'));
@fail.push('cwd direct') unless $p4.out.slurp(:close).trim.ends-with('tmp');
my $p5 = run('sh', '-c', 'read x; pwd', :in, :out, :cwd('/tmp'));
$p5.in.print("go\n");
@fail.push('cwd deferred') unless $p5.out.slurp(:close).trim.ends-with('tmp');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
