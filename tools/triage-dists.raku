#!/usr/bin/env rakupp
# Triage the self-test-fail dists: run each one's suite and record the FIRST
# thing that went wrong, so shared root causes show up as repeated signatures.
sub MAIN(Str $list, Str :$rakupp = '/Users/ash/raku++/build/rakupp', Int :$timeout = 240) {
    for $list.IO.lines.grep(*.trim.chars) -> $name {
        my $proc = Proc::Async.new($rakupp, 'test', $name);
        my $out = '';
        $proc.stdout.tap({ $out ~= $_ });
        $proc.stderr.tap({ $out ~= $_ });
        my $p = $proc.start;
        await Promise.anyof($p, Promise.in($timeout));
        if $p.status != Kept {
            $proc.kill(9);
            try await Promise.anyof($p, Promise.in(10));
            say "$name\ttimeout\t";
            next;
        }
        # the failing FILE, then the first diagnostic under it. `rakupp test`
        # prints the diagnostics INDENTED beneath the "FAILED:" line, so collect
        # those; a bare error (an exception) says more than "# Failed test" does.
        my $file = ($out ~~ /^^ 'FAILED: ' (\N+)/) ?? ~$0 !! '';
        my (@bare, @named);
        my $seen = False;
        for $out.lines {
            if /^ 'FAILED: '/ { $seen = True; next }
            next unless $seen;
            last unless /^ \s/ || /^ '#'/;          # the indented block ended
            my $t = .trim;
            next unless $t.chars;
            if $t.starts-with('# Failed test') { @named.push($t) }
            elsif !$t.starts-with('#') { @bare.push($t) }
        }
        my $why = (@bare || @named).head // '';
        $why .= subst(/ '/var/folders/' \S+ /, '<tmp>', :g);
        $why .= subst(/ ' at ' \S+ ' line ' \d+ /, '', :g);
        $why = $why.substr(0, 100);
        say "$name\t$file\t$why";
        $*OUT.flush;
    }
}
