# A COMPILED binary is the program — GitHub issue #86.
#
# Two halves of one complaint, both of them a binary describing itself as if it
# were still a script:
#
#   1. the usage text differed from the interpreter's. The `#=` option list was
#      missing, because the compile modes lexed the source and then never handed
#      the parser the declarator pod the lexer had just read — so every param's
#      description was gone before &MAIN's signature was ever written down;
#   2. the first usage line named the .raku file the binary was built from, by
#      absolute path, instead of the command the reader had typed. On the
#      reporter's machine `./analyze_endless` announced itself as
#      `/home/sibl/Downloads/analyze_endless.raku` — a path they were not in,
#      for a file the binary does not need and may outlive.
#
# The source path belongs to $?FILE and to diagnostics; $*PROGRAM-NAME belongs
# to argv[0], which is the thing a reader can type again. $*EXECUTABLE is that
# same binary resolved: a compiled program is its own interpreter, and used to
# answer with the empty string under --exe.
#
# All three compile modes are checked, because all three produce a standalone
# program: --exe (native transpilation), --bundle (source + interpreter) and
# --aot (embedded AST). Needs a C++ toolchain, as the other compiling cases do.

# The engine, by the name it answers to — the house idiom. This used to read
# the `--version` banner for the string "rakupp", which stopped appearing in it,
# and every compiled-mode case in t/regression/ silently skipped itself.
my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    # only rakupp compiles; under Rakudo there is nothing here to test
    note 'compiled-program-name: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("compiled-program-name-$*PID");
mkdir $work;

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eq $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}

my @made;
# Compile $src in $mode; returns the binary's path, or '' (having counted a
# failure) when the compile did not produce one.
sub compile(Str $mode, IO() $src, Str $name --> Str) {
    my $bin = $work.add($name);
    my $p = run $*EXECUTABLE, "--$mode", '-o', $bin.Str, $src.Str, :out, :err;
    $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    if $p.exitcode != 0 {
        $fails++;
        note "--$mode compile of {$src.basename} failed:\n$err";
        return '';
    }
    @made.push($bin.Str);
    $bin.Str
}

# the usage text a failed dispatch writes to stderr
sub usage-of(*@cmd) {
    my $p = run |@cmd, :out, :err;
    $p.out.slurp(:close);
    $p.err.slurp(:close).chomp
}

# ---- the usage text --------------------------------------------------------
my $usage-src = $work.add('usage86.raku');
$usage-src.spurt(q:to/END/);
    sub MAIN(
        $input,          #= input file
        UInt :$top = 5,  #= longest waiter to list
    ) {
        say "in=$input top=$top";
    }
    END
@made.push($usage-src.Str);

# the interpreter's own answer, which is what the binaries have to match
my $interp = usage-of($*EXECUTABLE, $usage-src.Str);
my $options = "  \n    <input>         input file"
            ~ "\n    --top=<UInt>    longest waiter to list [default: 5]";
check('the interpreter documents each parameter', $interp.lines[2..*].join("\n"), $options);
check('the interpreter names the script it was given',
      $interp.lines[1], "  {$usage-src.Str} [--top=<UInt>] <input>");

for <exe bundle aot> -> $mode {
    my $bin = compile($mode, $usage-src, "usage86-$mode") or next;
    my $u = usage-of($bin);
    check("--$mode: the option list survives the compile", $u.lines[2..*].join("\n"), $options);
    check("--$mode: the usage names the binary, not the source",
          $u.lines[1], "  $bin [--top=<UInt>] <input>");
}

# ---- and the variables the usage line is built from ------------------------
my $id-src = $work.add('progid86.raku');
$id-src.spurt('sub MAIN() { say $*PROGRAM-NAME; say $*EXECUTABLE.Str; say $?FILE }');
@made.push($id-src.Str);

for <exe bundle aot> -> $mode {
    my $bin = compile($mode, $id-src, "progid86-$mode") or next;
    my $p = run $bin, :out, :err;
    my @l = $p.out.slurp(:close).lines;
    $p.err.slurp(:close);
    check("--$mode: \$*PROGRAM-NAME is the binary as invoked", @l[0] // '', $bin);
    # $*EXECUTABLE is realpath'd, and $*TMPDIR itself may be a symlink, so the
    # comparable part is the name — plus the fact that there IS one (it was "").
    check("--$mode: \$*EXECUTABLE is that same binary",
          (@l[1] // '').IO.basename, "progid86-$mode");
    check("--$mode: \$*EXECUTABLE is an absolute path",
          ((@l[1] // '') ne '' && (@l[1] // '').IO.is-absolute), True);
    check("--$mode: \$?FILE still names the source", (@l[2] // '').IO.basename, 'progid86.raku');
}

unlink $_ for @made;
try rmdir $work;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
