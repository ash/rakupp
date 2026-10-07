# Run every sigil-free example through bin/sigilless on the given engines and
# compare with its .expected — the output of the hand-sigiled twin in
# reference/, made on Rakudo, so the translator never grades itself.
#   raku t/examples.raku /opt/homebrew/bin/rakudo /path/to/rakupp
my $root = $*PROGRAM.parent.parent;
my %args = main => <--shout bob 3>;
my @engines = @*ARGS || ~$*EXECUTABLE;
my ($pass, $fail) = 0, 0;
for @engines -> $engine {
    for $root.add('examples').dir(test => *.ends-with('.raku')).sort -> $ex {
        my $name = $ex.basename.subst(/'.raku' $/, '');
        my $want = $root.add("examples/$name.expected").slurp;
        my $p = run $engine, "-I{$root.add('lib')}", $root.add('bin/sigilless'), $ex,
                    |(%args{$name} // ()), :out, :err, :merge;
        my $got = $p.out.slurp(:close);
        if $got eq $want {
            $pass++;
            say "ok      {$engine.IO.basename} $name";
        }
        else {
            $fail++;
            say "NOT OK  {$engine.IO.basename} $name";
            say $got.lines.map('    got  | ' ~ *).join("\n");
            say $want.lines.map('    want | ' ~ *).join("\n");
        }
    }
}
say "PASS $pass FAIL $fail";
exit $fail ?? 1 !! 0;
