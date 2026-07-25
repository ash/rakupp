# Regression: the four output/semantics cleanups from the zef ecosystem work.
#   1. `run(:!out, :!err)` DISCARDS those streams (a probe stays silent); an
#      unspecified stream is still inherited, `:out`/`:err` still capture.
#   2. KEEP fires only on a successful block exit, UNDO only on an unsuccessful
#      one; LEAVE always. (Both used to fire, so zef logged "Updated <mirror>"
#      and "Failed to update <mirror>" for the same fetch.)
#   3. `<( … )>` inside a GRAMMAR RULE body trims what the capture reports, while
#      the parse continues at the real end (zef printed ver<<2.2.3>>).
#   4. a failed runtime `require` throws instead of also warning, so
#      `try require ::($m)` is silent.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. run adverbs
my $p = run('git', '--help', :!out, :!err);
@fail.push('run-silent-exit') unless $p.exitcode == 0;
@fail.push('run-capture') unless run('echo', 'hi', :out).out.slurp.chomp eq 'hi';

# 2. KEEP/UNDO
my @log;
sub good { KEEP @log.push('K'); UNDO @log.push('U'); LEAVE @log.push('L'); 1 }
good();
@fail.push("keep-path ({@log.join(',')})") unless @log eqv ['L', 'K'];
@log = ();
sub bad { KEEP @log.push('K'); UNDO @log.push('U'); LEAVE @log.push('L'); die 'x' }
try { bad() };
@fail.push("undo-path ({@log.join(',')})") unless @log eqv ['L', 'U'];
@log = ();
(1, 2).grep: -> $x { KEEP @log.push("K$x"); UNDO @log.push("U$x"); $x > 1 };
@fail.push("grep-phasers ({@log.join(',')})") unless @log eqv ['K1', 'K2'];

# 3. <( )> in a rule body
grammar G { regex v { x <( \w+ )> }; regex TOP { ^ <v> $ } }
my $m = G.parse('xabc');
@fail.push('rule-capture-trim') unless $m && ~$m<v> eq 'abc';
grammar H { regex v { '<' ~ '>' [<( \w+ )>] }; regex TOP { ^ <v> $ } }
my $h = H.parse('<abc>');
@fail.push('rule-trim-tilde') unless $h && ~$h<v> eq 'abc';

# 4. silent failed require
@fail.push('require-nil') unless (try require ::('No::Such::Module::Here')) ~~ Nil;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
