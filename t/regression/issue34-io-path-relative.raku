# Issue #34, the last thing between rakupp and `install TAP`: TAP's
# SourceHandler round-trips a path through `$cwd.add($path.relative($cwd))`.
#
# `.relative` only handled the PREFIX case — a path that was not under the base
# came back UNCHANGED, so the round-trip produced `/base//abs/path` and the file
# vanished. On macOS that is the common case, not the exotic one: `/var` is a
# symlink to `/private/var`, so a $*CWD under $TMPDIR (which rakupp resolves)
# shares no prefix with a path spelled `/var/...`, and TAP's own test suite
# failed during `rakupp install` while passing everywhere else.
#
# Rakudo's abs2rel is purely lexical: walk out of the base with `..` for every
# component the two do not share, then down into the path.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

check '/a/b/c/d.txt'.IO.relative('/a/b'), 'c/d.txt',        'the prefix case, which always worked';
check '/a/b/c.txt'.IO.relative('/a/b/'),  'c.txt',          'a trailing slash on the base';
check '/a/b'.IO.relative('/a/b'),         '.',              'the path IS the base';
check '/a/b/c'.IO.relative('/a/x/y'),     '../../b/c',      'a sibling branch walks out and back in';
check '/a'.IO.relative('/a/b/c'),         '../..',          'the path is an ANCESTOR of the base';
check '/a/b/c.txt'.IO.relative('/'),      'a/b/c.txt',      'the root as base';
check '/var/f/x.t'.IO.relative('/private/var/f'),
      '../../../var/f/x.t',
      'the macOS /var vs /private/var case that broke `install TAP`';

# The round-trip callers rely on, against the real filesystem: whatever the two
# spellings of the base are, `$base.add($path.relative($base))` must still name
# the file. This is the assertion that failed inside `rakupp install`.
my $real = $*TMPDIR.add("rakupp-relative-probe-{$*PID}.txt");
$real.spurt('x');
LEAVE { try $real.unlink }
for $*TMPDIR.Str, $*TMPDIR.absolute, '/' -> $b {
    check $b.IO.add($real.Str.IO.relative($b)).e, True,
          "\$base.add(\$p.relative(\$base)) still names the file, base $b";
}

# the default base is the call-time $*CWD
my $here = $*CWD.Str;
check "$here/zz".IO.relative, 'zz', 'the default base is $*CWD';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
