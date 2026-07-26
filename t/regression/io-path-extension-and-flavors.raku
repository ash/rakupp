# Regression: IO::Path's extension-REPLACING form, path cleanup, and the
# per-OS path flavors.
#   * `.extension($new, :parts, :joiner)` replaces instead of reading. The joiner
#     defaults to "" for an empty replacement and "." otherwise — which is why
#     `.extension('')` takes the dot with it but `:joiner<_>` leaves one behind.
#     If no extension of the requested size exists, nothing is replaced at all.
#   * `.cleanup` squeezes repeated separators and drops `.` segments — but NOT
#     `..`, which may cross a symlink and so cannot be resolved textually. It
#     answers an IO::Path, not a Str.
#   * `.add`/`.child` take SEVERAL parts (or one list), appending each as a
#     segment.
#   * a flavored path (IO::Path::Win32 and friends) keeps its flavor in the same
#     field an enum uses for its key, so `.Str`/`.gist` used to answer "Win32".
#     `.basename`/`.parts` route through that flavor's IO::Spec, which is what
#     makes a UNC volume come out whole.
#   * `IO::Path.new(…, :CWD(…))` remembers the directory it was given.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# .extension — replacing
check("foo.tar.gz".IO.extension('').gist,               '"foo.tar".IO',        'extension-replace-with-empty');
check("foo.tar.gz".IO.extension('ZIP').gist,            '"foo.tar.ZIP".IO',    'extension-replace');
check("foo.tar.gz".IO.extension('ZIP', :0parts).gist,   '"foo.tar.gz.ZIP".IO', 'extension-zero-parts-appends');
check("foo.tar.gz".IO.extension('ZIP', :2parts).gist,   '"foo.ZIP".IO',        'extension-replace-two-parts');
check("foo.tar.gz".IO.extension('ZIP', :parts(^5)).gist, '"foo.ZIP".IO',       'extension-range-takes-what-there-is');
check("foo.tar.gz".IO.extension('ZIP', :5parts).gist,   '"foo.tar.gz".IO',     'extension-absent-size-replaces-nothing');
# a non-default joiner
check("foo.tar.gz".IO.extension('', :joiner<_>).gist,             '"foo.tar_".IO',    'extension-joiner-with-empty');
check("foo.tar.gz".IO.extension('ZIP', :joiner<_>).gist,          '"foo.tar_ZIP".IO', 'extension-joiner');
check("foo.tar.gz".IO.extension('ZIP', :joiner<_>, :2parts).gist, '"foo_ZIP".IO',     'extension-joiner-two-parts');
# leading dots, and an empty result
check("...".IO.extension('tar').gist,              '"...tar".IO', 'extension-after-leading-dots');
check("...".IO.extension('tar', :joiner('')).gist, '"..tar".IO',  'extension-empty-joiner');
check(".".IO.extension('').gist,                   '".".IO',      'extension-emptied-basename-is-a-dot');
check("a/b/foo.txt".IO.extension('md').gist,       '"a/b/foo.md".IO', 'extension-keeps-the-directory');
# the READING form is unchanged
check("foo.tar.gz".IO.extension,            'gz',     'extension-read');
check("foo.tar.gz".IO.extension(:parts(2)), 'tar.gz', 'extension-read-two-parts');
check("foo.tar.gz".IO.extension(:parts(9)), '',       'extension-read-more-than-there-is');

# .cleanup
check("foo/./././..////bar".IO.cleanup.gist, '"foo/../bar".IO', 'cleanup');
check("a/b/../c".IO.cleanup.gist,            '"a/b/../c".IO',   'cleanup-keeps-dotdot');
check("./a".IO.cleanup.gist,                 '"a".IO',          'cleanup-drops-a-leading-dot');
check("a/b//c/".IO.cleanup.gist,             '"a/b/c".IO',      'cleanup-squeezes-separators');

# .add with several parts
check("foo".IO.add(<bar baz>).gist, '"foo/bar/baz".IO', 'add-a-list-of-parts');
check("foo".IO.add('bar').gist,     '"foo/bar".IO',     'add-one-part');
check("foo".IO.child('bar').gist,   '"foo/bar".IO',     'child-is-the-same');

# path flavors
check(IO::Path::Win32.new('a/b').Str,   'a/b',              'a-flavored-path-strs-as-its-path');
check(IO::Path::Win32.new('a/b').^name, 'IO::Path::Win32',  'and-knows-its-class');
check(IO::Path::Win32.new('//server/share').basename, '\\', 'win32-unc-basename');
check(IO::Path::Win32.new('C:/rakudo/raku.bat').parts.raku,
      'IO::Path::Parts.new("C:","/rakudo","raku.bat")', 'win32-parts-split-the-volume');
check(IO::Path::Win32.new('foo/./././..////bar').cleanup.gist, '"foo\\..\\bar".IO', 'win32-cleanup');
check("a/b/c.txt".IO.parts.raku, 'IO::Path::Parts.new("","a/b","c.txt")', 'unix-parts-are-unchanged');
# a backslash is an ordinary path character, not something to escape
check(｢a\b｣.IO.gist, '"a\\b".IO', 'a-backslash-shows-verbatim');

# an explicit :CWD
check(IO::Path.new("foo", :CWD</home/camelia>).CWD, '/home/camelia', 'explicit-cwd');
check(IO::Path.new("foo", :CWD</home/camelia>).raku,
      'IO::Path.new("foo", :SPEC(IO::Spec::Unix), :CWD("/home/camelia"))', 'explicit-cwd-in-raku');
check("x".IO.CWD.chars > 0, 'True', 'the-default-cwd-still-works');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
