# Once wrong: mkdir swallowed every failure and answered success.
#
# Both forms — the sub and $path.IO.mkdir — ran a mkdir-p loop, ignored every
# return value, and handed back the path (the sub as a Str, which was wrong
# on success too: Rakudo answers an IO::Path). So issue #26's repro exited 0
# under rakupp where Rakudo dies: its `mkdir $root` in sink context detonates
# Rakudo's Failure, and rakupp had nothing to detonate.
#
# Semantics pinned against Rakudo 2026.08: parents are created as needed; an
# already-existing DIRECTORY is success; success answers the IO::Path from
# both forms; mode rides positionally on the sub and as $mode / :mode on the
# method, and appears in the failure message. Failure is SOFT — X::IO::Mkdir
# with "Failed to create directory '<path>' with mode '0oNNN': ..." — False
# when boolified, deadly when sunk. Failure probes here go through paths
# shadowed by a regular FILE (EEXIST / ENOTDIR), so they fail for root too.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my $dir = $*TMPDIR.add("mkdir-gate-$*PID");
LEAVE { run 'rm', '-rf', $dir.Str }

# --- success answers an IO::Path, from BOTH forms -----------------------------
check mkdir($dir.Str).^name, 'IO::Path', 'sub-form success is an IO::Path (was Str)';
check $dir.add('b').mkdir.^name, 'IO::Path', 'method-form success is an IO::Path';
check mkdir($dir.Str).^name, 'IO::Path', 'an existing directory is success';
check $dir.add('p/q/r').mkdir.^name, 'IO::Path', 'parents are created as needed';
check $dir.add('p/q/r').d, True, '...and exist';

# --- mode rides through — onto the directory and into the message -------------
$dir.add('m1').mkdir(:mode(0o700));
check $dir.add('m1').mode, '0700', 'method :mode(0o700) is honored';
$dir.add('m2').mkdir(0o700);
check $dir.add('m2').mode, '0700', 'method positional mode is honored';
check mkdir($dir.add('m3').Str, 0o700).^name, 'IO::Path', 'the sub takes positional mode';
check $dir.add('m3').mode, '0700', '...and honors it';

# --- failure is a soft Failure, typed and worded like Rakudo's ----------------
my $file = $dir.add('afile');
$file.spurt("x");
my $f1 = mkdir $file.Str;
check $f1.^name, 'Failure', 'mkdir on an existing file is a Failure';
check ?$f1, False, '...that boolifies False';
check $file.add('sub').mkdir.^name, 'Failure', 'a path under a file is a Failure (method form)';
try { mkdir $file.Str; };
check $!.^name, 'X::IO::Mkdir', 'sunk in try: X::IO::Mkdir';
check $!.message.starts-with("Failed to create directory '{$file}' with mode '0o777': "), True,
    'Rakudo\'s message shape, default mode named';
try { $file.IO.mkdir(:mode(0o700)); };
check $!.message.contains("with mode '0o700'"), True, 'the requested mode is in the message';

# --- a bare sunk mkdir DETONATES — the issue-26 shape, once exit 0 ------------
my $p = run $*EXECUTABLE, '-e', 'mkdir "' ~ $file ~ '/sub"; say "survived"', :out, :err;
my $out = $p.out.slurp(:close);
my $err = $p.err.slurp(:close);
check $p.exitcode == 0, False, 'a sunk failing mkdir kills the program';
check $out.contains('survived'), False, '...before the next statement runs';
check $err.contains('Failed to create directory'), True, '...with the message on stderr';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
