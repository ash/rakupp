#!/usr/bin/env rakupp
# A note keeper with subcommands: one `multi MAIN` per verb, and the verb is a
# literal in the signature — the dispatcher picks the candidate, so there is no
# `given $command` anywhere in this file.

my %*SUB-MAIN-OPTS = :named-anywhere;

my $FILE = %*ENV<NOTES_FILE> // 'notes.txt';

sub notes() { $FILE.IO.e ?? $FILE.IO.lines.Array !! [] }

#| add a note
multi MAIN('add', *@words) {
    my @notes = notes();
    @notes.push('[ ] ' ~ @words.join(' '));
    spurt $FILE, @notes.join("\n") ~ "\n";
    say "added {@notes.elems}: {@words.join(' ')}";
}

#| list notes, open ones only unless --all
multi MAIN('list', Bool :$all = False) {
    my @notes = notes();
    for @notes.kv -> $i, $note {
        next if !$all && $note.starts-with('[x]');
        say sprintf('%2d  %s', $i + 1, $note);
    }
}

#| mark a note done
multi MAIN('done', Int $n) {
    my @notes = notes();
    unless 1 <= $n <= @notes {
        note "notes: no note $n";
        exit 2;
    }
    @notes[$n - 1] .= subst('[ ]', '[x]');
    spurt $FILE, @notes.join("\n") ~ "\n";
    say "done $n";
}
