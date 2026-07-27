# Regression: `{…}` code blocks inside a PLAIN regex (the grammar path already had them).
#   * a block runs when the ENGINE REACHES IT, not when the match is accepted — so a
#     branch the engine later abandons still ran its block, and a retried branch runs
#     it again. `/a+ {…} b/` against 'aaa' fires six times and matches nothing.
#   * `$/` and `$¢` inside the block are the CURSOR: the match as far as the engine
#     has got, positional and named captures included. `$¢` exists only for the
#     block's duration — outside a regex it is Nil.
#   * `.pos` is where the engine has got to, i.e. the cursor's end.
#   * a `$var` inside the block belongs to the BLOCK. Interpolating `$var` into the
#     pattern text (which is right for a `$var` ATOM) rewrote `{ $c = 42 }` into
#     `{ 1 = 42 }`, so every write died with X::Assignment::RO and every read saw a
#     stale copy. Blocks and `<?{…}>` assertions are now skipped when interpolating.
#   * the hooks are wired before the `:g`/`:nth` loops, not after, so those run
#     blocks too.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# a block fires on every branch the engine walks, accepted or not
my @log;
'aaa' ~~ / a+ { @log.push('x') } b /;
check(@log.elems, '6', 'an abandoned branch still ran its block');
@log = ();
'aab' ~~ / a+ { @log.push('x') } b /;
check(@log.elems, '1', 'a match that succeeds first try runs it once');
@log = ();
'abcabc' ~~ m:g/ a { @log.push('g') } b /;
check(@log.elems, '2', ':g runs the block per match');

# $/ and $¢ are the cursor
my $c;
'abc' ~~ /.$${ $c = $¢ }/;
check($c.Str, 'c', 'the cursor is the match so far');
'camelia' ~~ /<[ l m ]> { $c = $¢ }/;
check($c.Str,  'm', 'and starts where THIS attempt started, not at 0');
check($/.Str,  'm', 'the finished match agrees');
my @seen;
'123' ~~ / (\d) { @seen.push($0.Str); @seen.push($/.Str) } \d+ /;
check(@seen.join(','), '1,1', 'a block sees the captures made so far');
check($¢.defined, 'False', 'the cursor is gone once the match is over');

# .pos
my $p;
'abcdef' ~~ /b. { $p = $/.pos } ../;
check($p, '3', 'pos is the end of the cursor');

# a $var in a block is the block's own variable
my $q = 1;
'camelia' ~~ /m { $q = 42 }/;
check($q, '42', 'a block assigns to an outer lexical');
my $r = 'zz';
'camelia' ~~ /m { $r = $r ~ '!' }/;
check($r, 'zz!', 'and reads it live rather than as pattern text');
# a `$var` ATOM still interpolates — that is what the skip must not break
my $atom = 'mel';
check(('camelia' ~~ /$atom/).Str, 'mel', 'a variable atom still matches its value');
check(('ca.melia' ~~ /$atom/).Str, 'mel', 'and is still quotemeta-ed');
my @alts = <xx lia>;
check(('camelia' ~~ /@alts/).Str, 'lia', 'an array atom still alternates');

# make inside a plain regex still reaches .made
my $m = 'abc' ~~ / (\w) { make $0.Str.uc } /;
check($m.made, 'A', 'an inline make still lands on the match');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
