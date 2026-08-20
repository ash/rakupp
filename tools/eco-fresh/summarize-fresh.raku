#!/usr/bin/env rakupp
# summarize-fresh.raku — turn a sweep's results.tsv + logs into the tallies.
#
#   rakupp summarize-fresh.raku --logs=DIR results.tsv > REPORT.md
#
# Four tables: the verdict histogram, what blocks the dependency failures (one
# dist that fails its own suite blocks every dist above it), the error
# signature of each dist that failed on its own account — read back out of its
# log and normalized so the recurring shapes group — and the full roster.

sub logfile($logs, $name) { $logs.IO.add($name.subst('::', '-', :g) ~ '.log') }

# Everything the log says after the target's own suite started: the lines
# before that belong to its dependencies and are a different dist's problem.
sub own-section($log, $target) {
    my @lines = $log.lines;
    my $start = 0;
    for @lines.kv -> $i, $l {
        $start = $i if $l.starts-with("testing $target:") || $l.starts-with("building $target:");
    }
    @lines[$start .. *]
}

sub tidy($s) {
    $s.subst(/ '/var/folders/' <-[\s]>+ '/' /, '', :g).subst(/\s+/, ' ', :g).trim
}

sub signature(@sec) {
    my ($failed, $of) = 0, 0;
    for @sec -> $l {
        my $t = $l.trim;
        if $t.starts-with('===SORRY!===') || $t ~~ /'Parse error at line'/ {
            my $m = $t ~~ /'at line ' \d+ ':' \s* (.*)/ ?? ~$0 !! $t;
            $m = $m.subst(/^ 'Error while compiling module ' \S+ ' (line ' \d+ '): '/, '');
            return "parse: " ~ tidy($m);
        }
        if $t ~~ /'Cannot load native library'/                  { return 'native library missing (environment)' }
        if $t ~~ /"No such method '" (<-[']>+ ) "' for invocant of type '" (<-[']>+) "'"/ {
            return "no such method: .{~$0} on {~$1}";
        }
        if $t ~~ /"No such method '" (<-[']>+) "'"/              { return "no such method: .{~$0}" }
        if $t ~~ /'Could not find ' (\S+)/                       { return "module not found: {~$0}" }
        if $t ~~ /"Undefined routine '" (<-[']>+) "'"/           { return "undefined routine: {~$0}" }
        if $t ~~ /'Type ' (\S+) ' does not support ' (.*)/       { return "{~$0} does not support {tidy(~$1)}" }
        if $t ~~ /'Type check failed in ' (\w+) ' to ' (.+?) ';' \s* 'expected ' (\S+) ' but got ' (\S+)/ {
            return "type check ({~$0} to {~$1}): expected {~$2}, got {~$3}";
        }
        if $t ~~ /"Variable '" (<-[']>+) "' is not declared"/    { return "undeclared variable: {~$0}" }
        if $t ~~ /'Too many levels of recursion'/                { return 'too many levels of recursion' }
        if $t ~~ /'Type check failed' (.*)/                      { return 'type check failed' ~ tidy(~$0).substr(0, 50) }
        if $t ~~ /'Cannot resolve caller' \s* (.*)/              { return "cannot resolve caller: " ~ tidy(~$0).substr(0, 60) }
        # "1 test of 14" as well as "15 tests of 23", with or without TAP's '#'
        if $t ~~ /'Looks like you failed ' (\d+) ' test' 's'? ' of ' (\d+)/ { ($failed, $of) = (+$0, +$1) }
    }
    # nothing matched a known shape: the line the harness printed under the
    # first failing file is the module's own death, and reads better raw than
    # any bucket we could invent for it
    return "assertions: $failed of $of tests failed" if $of;
    my $seen = False;
    for @sec -> $l {
        my $t = $l.trim;
        if $t.starts-with('FAILED: ') || $t.starts-with('BUILD FAILED: ') { $seen = True; next }
        next unless $seen;
        next if $t.starts-with('#') || !$t.chars;
        next if $t.starts-with('ok ') || $t.starts-with('not ok ');
        return tidy($t).substr(0, 90);
    }
    '(nothing captured)'
}

sub MAIN($results, :$logs!) {
    my @rows = $results.IO.lines.skip(1).map({ .split("\t") }).grep({ .[3] });
    for @rows -> @r {
        if @r[6] eq '-1' {
            # what it was doing when the budget ran out is recorded either as
            # the last log line (this run) or already in the detail column (a
            # row restored from an earlier pass) — normalize both to the name
            my $stuck = @r[4] || @r[7] || '';
            $stuck = $stuck.subst(/^ ['testing ' | 'building '] /, '').subst(/':' \s* \d+ .* $/, '').trim;
            @r[4] = $stuck;
            @r[3] = 'timeout';
            @r[7] = '';
        }
    }

    say "# `rakupp test` over the 100 most recently released distributions\n";
    say "{+@rows} dists.\n";

    my %v;
    %v{.[3]}++ for @rows;
    say "## Verdicts\n";
    say "| verdict | dists |";
    say "|---|---:|";
    say "| $_ | {%v{$_}} |" for %v.keys.sort({ %v{$^b} <=> %v{$^a} });
    say "";

    my @pass = @rows.grep({ .[3] eq 'pass' }).map({ .[0] }).sort;
    say "**Green:** {@pass.join(', ')}\n" if @pass;

    my @dep = @rows.grep({ .[3].starts-with('dep') || .[3] eq 'timeout' });
    if @dep {
        my %b;
        %b{.[4] || '(unknown)'}++ for @dep;
        say "## What blocks the {+@dep} dists that never reached their own suite\n";
        say "A dependency that fails its own suite, or takes longer than the budget,";
        say "stops the dist above it before a single line of its tests runs.\n";
        say "| blocking dist | blocks |";
        say "|---|---:|";
        say "| $_ | {%b{$_}} |" for %b.keys.sort({ %b{$^b} <=> %b{$^a} || $^a leg $^b });
        say "";
    }

    my @self = @rows.grep({ .[3] eq 'self-fail' | 'build-fail' | 'other' });
    if @self {
        my %s;
        for @self -> @r {
            my $f = logfile($logs, @r[0]);
            my $sig = $f.e ?? signature(own-section($f.slurp, @r[0])) !! '(no log)';
            %s{$sig}.push(@r[0]);
        }
        say "## Error signatures — the {+@self} dists that fail on their own account\n";
        say "| dists | signature | which |";
        say "|---:|---|---|";
        for %s.keys.sort({ +%s{$^b} <=> +%s{$^a} || $^a leg $^b }) -> $sig {
            my @who = %s{$sig}.sort;
            say "| {+@who} | {$sig} | {@who.join(', ')} |";
        }
        say "";
    }

    say "## Every dist, newest release first\n";
    say "| released | dist | version | verdict | blocked by / first error |";
    say "|---|---|---|---|---|";
    for @rows.sort({ $^b[2] leg $^a[2] || $^a[0] leg $^b[0] }) -> @r {
        my $note = @r[4] ?? @r[4] !! (@r[7] // '');
        $note = tidy($note).subst('|', '\\|', :g);
        $note = $note.substr(0, 110) ~ ' …' if $note.chars > 110;
        say "| {@r[2]} | {@r[0]} | {@r[1]} | {@r[3]} | $note |";
    }
}
