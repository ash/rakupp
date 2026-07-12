#!/usr/bin/env raku
# Compare Raku++ against Rakudo on RosettaCode Raku solutions.
#   raku tools/rc-compare.raku [N=40] [skip=0] [rakupp=./build/rakupp]
# Fetches Category:Raku tasks, extracts each first Raku solution, runs it under
# both `raku` and rakupp (10s timeout, closed stdin), and tallies MATCH / DIFFER
# / rakupp-error / rakudo-error / …  Results stream to rc-work/results.tsv.
# See docs/dev/ROSETTACODE.md for a written-up run.

my $N      = (@*ARGS[0] // 40).Int;
my $SKIP   = (@*ARGS[1] // 0).Int;
my $RAKUPP = @*ARGS[2] // './build/rakupp';
my $DIR    = 'rc-work';
mkdir "$DIR/prog";

# Persisted, git-ignored cache of fetched RosettaCode programs, keyed by task NAME
# (stable across different N/skip, unlike the old prog/<index>.raku). A task fetched
# once is reused forever — no re-fetch. See docs/dev/ROSETTACODE.md.
my $CACHE = 'rc-cache';
mkdir $CACHE;
sub cache-file($task) { "$CACHE/" ~ $task.subst(/<-[\w.\-]>+/, '_', :g) ~ '.raku' }

# A tiny timeout wrapper (macOS has no `timeout`): run CMD with stdin closed,
# stdout/stderr to files, killed after SECS. Exit 137 == killed.
my $TO = "$DIR/timeout.sh";
$TO.IO.spurt: Q:to/SH/;
#!/bin/sh
secs=$1; of=$2; ef=$3; shift 3
"$@" </dev/null >"$of" 2>"$ef" &
pid=$!
( sleep "$secs"; kill -9 "$pid" 2>/dev/null ) & killer=$!
wait "$pid" 2>/dev/null; ec=$?
kill "$killer" 2>/dev/null
exit $ec
SH

# Task list (paginated Category:Raku), fetched once.
my $tasks = "$DIR/tasks.txt";
unless $tasks.IO.e {
    my $cont = ''; my @t;
    for ^40 {
        my $u = "https://rosettacode.org/w/api.php?action=query&list=categorymembers"
              ~ "&cmtitle=Category:Raku&cmlimit=500&format=json"
              ~ ($cont ?? "&cmcontinue=$cont" !! '');
        my $r = qqx{curl -s -m 30 '$u'};
        @t.append: $r.match(/'"title":"' (<-["]>+) '"'/, :g).map(~*[0]);
        with $r.match(/'"cmcontinue":"' (<-["]>+) '"'/) { $cont = ~$0 } else { last }
    }
    $tasks.IO.spurt: @t.unique.sort.join("\n");
}
my @tasks = $tasks.IO.lines[$SKIP ..^ ($SKIP + $N)];

sub run-prog($bin, $file, $tag) {
    my $of = "$DIR/o-$tag.txt"; my $ef = "$DIR/e-$tag.txt";
    my $p = run('sh', $TO, '10', $of, $ef, $bin, $file);
    my $out = $of.IO.e ?? $of.IO.slurp !! '';
    return ($out, $p.exitcode);        # exit 137 == timed out (killed)
}

sub extract($wt) {
    return Nil unless $wt ~~ / '=={{header|Raku}}==' (.*?) [ \n '=={{header|' | $ ] /;
    return Nil unless ~$0 ~~ / '<syntaxhighlight lang="raku"' <-[>]>* '>' (.*?) '</syntaxhighlight>' /;
    ~$0.subst('&lt;','<',:g).subst('&gt;','>',:g).subst('&amp;','&',:g)
       .subst('&quot;','"',:g).subst('&#039;',"'",:g);
}

my %tally;
my $log = "$DIR/results.tsv".IO.open(:w);
$log.say("task\tcategory\trakudo_exit\trakupp_exit");
for @tasks.kv -> $i, $task {
    my $f = cache-file($task);
    my $code = $f.IO.e ?? $f.IO.slurp
             !! extract(try qqx{curl -s -m 25 -G 'https://rosettacode.org/w/index.php' --data-urlencode 'title=$task' --data 'action=raw'} // '');
    unless $code { %tally<no-code>++; $log.say("$task\tno-code\t\t"); $log.flush; next }
    $f.IO.spurt($code);
    my ($ro, $rx) = run-prog('raku', $f, 'rd');
    my ($uo, $ux) = run-prog($RAKUPP, $f, 'up');
    my $cat = $rx == 137 ?? 'rakudo-timeout' !! $rx != 0 ?? 'rakudo-error'
           !! $ux == 137 ?? 'rakupp-timeout' !! $ux != 0 ?? 'rakupp-error'
           !! $ro eq $uo ?? 'MATCH' !! 'DIFFER';
    %tally{$cat}++;
    $log.say("$task\t$cat\t$rx\t$ux"); $log.flush;
}
$log.close;
say "\n=== RosettaCode: Rakudo vs Raku++ ({@tasks.elems} tasks) ===";
printf "  %-16s %d\n", .key, .value for %tally.sort(-*.value);
