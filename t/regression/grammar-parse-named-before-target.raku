# A grammar's `.parse`/`.subparse` target is its first POSITIONAL argument,
# wherever the named ones stand (rakudo#2791): `C.subparse(:rule<k>, "C#")`
# parsed the `:rule` Pair, so every `<!before \S>` check in the issue failed.
# And an undefined target dies, as Rakudo's does.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

grammar Chord {
    regex key { <[A..G]> <[#b]>? }
    regex bare-key { <key> <!before \S > }
    regex bare-key-one { <key> [ <!before \S > | $ ] }
    regex bare-key-two { <key> <!before <:!Space> > }
    token TOP { <key> }
}

check Chord.subparse(:rule<bare-key>, 'C#').so, 'True', 'the issue: a named before the target (rakudo#2791)';
check Chord.subparse(:rule<bare-key>, 'C# m').so, 'True', '…before a space';
check Chord.subparse(:rule<bare-key>, 'C#m').so, 'False', '…and not before a letter';
check Chord.subparse(:rule<bare-key-one>, 'C#').so, 'True', 'an alternation with $';
check Chord.subparse(:rule<bare-key-two>, 'C#').so, 'True', 'a property';
check Chord.parse(:rule<key>, 'Bb').Str, 'Bb', '.parse too';
check Chord.parse('C#', :rule<key>).Str, 'C#', 'the named after the target, as before';
check Chord.parse(:actions(Any), 'D').Str, 'D', 'another named first';
check (try Chord.parse(Any)) // 'died', 'died', 'an undefined target dies';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
