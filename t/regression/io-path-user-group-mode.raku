# Regression: `fez upload` could not build a bundle (issue #74).
#
# fez's tar writer reads a file's owner and permissions for every header it
# packs, and rakupp answered none of that the way Rakudo does:
#
#   $f.user, $f.group   —  no such method at all, so bundling died outright
#   $f.mode             —  a plain Str "0644" where Rakudo has an IntStr, and
#                          the string face ran to five digits on a sticky
#                          directory ("01777" for /tmp, where Rakudo has "1777")
#
# The Str was the quieter half. fez packs `sprintf("%07o", $f.mode)`, and
# "0644" numifies to DECIMAL 644 — so had `.user` merely been added, every
# file in every bundle would have gone out with mode 0o1204 and no error
# anywhere. That is why the round-trip below is asserted and not just the type.
#
# The mode fixtures are chmod'd by the SYSTEM chmod and the ids read back from
# the SYSTEM ls, so no row's expected value comes from the engine under test.
# /etc/passwd is there because we do not own it: an engine that answered
# getuid() for every file would pass every other .user row.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# --- a Failure, not a throw, on a path that is not there ----------------------
# fez writes `$f.user // 1000`, so this arm has to stay soft.
my $gone = "/nonexistent/rakupp-{$*PID}".IO;
for <mode user group> -> $m {
    my $soft = $gone."$m"();
    check $soft.defined,                       False, ".$m of a missing path is undefined";
    check $soft.exception.^name, 'X::IO::DoesNotExist', "...an X::IO::DoesNotExist";
    check ($gone."$m"() // 1000),                1000, "...and `// 1000` reaches its default";
}

# --- POSIX ownership and permission bits --------------------------------------
unless $*DISTRO.is-win {
    my $dir = $*TMPDIR.add("io-stat-gate-$*PID");
    $dir.mkdir;
    LEAVE { run 'chmod', '-R', 'u+rwx', $dir.Str; run 'rm', '-rf', $dir.Str }

    sub chmod-ext($path, $bits) { run 'chmod', $bits, $path.Str }
    sub ls-ids($path) {                    # the OS's own answer, not ours
        my $p = run 'ls', '-ln', $path.Str, :out, :err;
        my $line = $p.out.slurp(:close); $p.err.slurp(:close);
        my @w = $line.words;
        @w >= 4 && @w[2] ~~ /^ \d+ $/ && @w[3] ~~ /^ \d+ $/ ?? (@w[2].Int, @w[3].Int) !! ()
    }

    # .mode is an allomorph, and each row gets its own file
    my %want = '0640' => 0o640, '0755' => 0o755;
    for %want.kv -> $spelling, $bits {
        my $f = $dir.add("m$spelling");
        $f.spurt('x');
        chmod-ext($f, $spelling);
        check $f.mode.WHAT.^name, 'IntStr',   "mode of a $spelling file is an IntStr";
        check $f.mode.Int,          $bits,    "...numerically 0o$spelling";
        check $f.mode.Str,      $spelling,    "...and \"$spelling\" as a string";
    }

    # fez's own expression — the one that silently wrote 0o1204 for 0o644
    my $packed = $dir.add('m0640');
    check sprintf("%07o", $packed.mode), '0000640', 'sprintf("%07o", .mode) is the tar header fez writes';

    # a sticky directory: four octal digits, where "0%03o" produced five
    my $sticky = $dir.add('sticky');
    $sticky.mkdir;
    chmod-ext($sticky, '1777');
    check $sticky.mode.Str,   '1777', 'a sticky directory has a FOUR-digit mode string';
    check $sticky.mode.Int, 0o1777,   '...and the sticky bit survives numerically';

    # .user / .group are plain Ints — the NAMES are $*USER and $*GROUP
    my $owned = $dir.add('owned');
    $owned.spurt('x');
    if ls-ids($owned) -> ($uid, $gid) {
        check $owned.user.WHAT.^name,  'Int', '.user is an Int, not an allomorph';
        check $owned.group.WHAT.^name, 'Int', '.group is an Int, not an allomorph';
        check $owned.user,             $uid,  '.user is what ls reports';
        check $owned.group,            $gid,  '.group is what ls reports';
        check $owned.user,      $*USER.Int,   '...which for a file we just made is our own uid';
    }

    # a file we do NOT own, so answering getuid() unconditionally fails here
    if '/etc/passwd'.IO.e && ls-ids('/etc/passwd'.IO) -> ($uid, $gid) {
        check '/etc/passwd'.IO.user,  $uid, '.user of a file we did not create';
        check '/etc/passwd'.IO.group, $gid, '.group of a file we did not create';
    }
}

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say 'PASS';
