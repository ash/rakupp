# Every program in errors/ must be refused, with a first line that says why.
#   raku t/errors.raku /opt/homebrew/bin/rakudo /path/to/rakupp
my $root = $*PROGRAM.parent.parent;
my %want =
    'capitalized'     => "'Total' cannot be a sigil-free variable: a capitalized name",
    'declared-vs-use' => "'a' is declared @a at line 1, but line 2: `a<k>` looks up a key",
    'keyword'         => "'say' cannot be a sigil-free variable",
    'number-push'     => 'push(Int:D',      # a run-time error: `0` made it a $, and Int has no .push
    'shadow'          => "'a' is \$a at line 3 but @a in an enclosing scope (line 1); one name, one kind",
    'two-kinds'       => "'thing' is @thing from line 1",
;
my @engines = @*ARGS || ~$*EXECUTABLE;
my ($pass, $fail) = 0, 0;
for @engines -> $engine {
    for %want.keys.sort -> $name {
        my $p = run $engine, "-I{$root.add('lib')}", $root.add('bin/sigilless'),
                    $root.add("errors/$name.raku"), :out, :err, :merge;
        my $first = $p.out.slurp(:close).lines.head // '';
        if $p.exitcode != 0 && $first.contains(%want{$name}) {
            $pass++;
            say "ok      {$engine.IO.basename} $name";
        }
        else {
            $fail++;
            say "NOT OK  {$engine.IO.basename} $name (exit {$p.exitcode})";
            say "    got  | $first";
            say "    want | …{%want{$name}}…";
        }
    }
}
say "PASS $pass FAIL $fail";
exit $fail ?? 1 !! 0;
