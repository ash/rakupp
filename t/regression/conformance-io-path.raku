# Regression: IO::Path rendering and the pieces of its API the docs demonstrate.
#   * `.gist` is the expression that makes one — `"foo/bar".IO` — and `.raku` the
#     full constructor with SPEC and CWD. `.Str` stays the bare path.
#   * `.extension(:parts(…))`: an Int asks for exactly that many dot-separated
#     tail segments (empty when the name has fewer); a Range asks for the LARGEST
#     count in it that the name has. A LEADING dot counts, so ".bashrc" has
#     extension "bashrc".
#   * `.SPEC` / `.CWD`, IO::Spec::Unix.new / .dir-sep, and IO::Spec type objects
#     gisting by their SHORT name.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# rendering
check("foo/bar".IO.gist, '"foo/bar".IO', 'io-path-gist');
check("foo/bar".IO.Str,  'foo/bar',      'io-path-str-is-bare');
check("a b".IO.gist,     '"a b".IO',     'io-path-gist-quotes-spaces');
check("foo/bar".IO.raku.starts-with('IO::Path.new("foo/bar", :SPEC(IO::Spec::Unix), :CWD('),
      'True', 'io-path-raku-is-the-constructor');

# .extension(:parts(N)) — exactly N, or nothing
check("foo.tar.gz".IO.extension,             'gz',     'extension-default');
check("foo.tar.gz".IO.extension(:parts(0)),  '',       'extension-parts-0');
check("foo.tar.gz".IO.extension(:parts(1)),  'gz',     'extension-parts-1');
check("foo.tar.gz".IO.extension(:parts(2)),  'tar.gz', 'extension-parts-2');
check("foo.tar.gz".IO.extension(:parts(3)),  '',       'extension-parts-more-than-it-has');
# a Range takes the largest count available within it
check("foo.tar.gz".IO.extension(:parts(0..2)), 'tar.gz', 'extension-range-caps-at-available');
check("foo.tar.gz".IO.extension(:parts(1..3)), 'tar.gz', 'extension-range-caps-high');
check("foo.tar.gz".IO.extension(:parts(2..*)), 'tar.gz', 'extension-range-open-ended');
# names with no extension, and leading dots
check("foo".IO.extension(:parts(1)), '',        'extension-none-available');
check("foo".IO.extension(:parts(0)), '',        'extension-zero-is-not-the-name');
check(".".IO.extension,              '',        'extension-of-a-lone-dot');
check("...tar".IO.extension,         'tar',     'extension-after-leading-dots');
check(".bashrc".IO.extension,        'bashrc',  'a-leading-dot-still-counts');

# SPEC / CWD and the IO::Spec types
check("x".IO.SPEC.gist,        '(Unix)',         'io-path-SPEC');
check("x".IO.SPEC.^name,       'IO::Spec::Unix', 'io-spec-full-name');
check("x".IO.CWD.chars > 0,    'True',           'io-path-CWD');
check(IO::Spec::Unix.dir-sep,  '/',              'unix-dir-sep');
check(IO::Spec::Win32.dir-sep, '\\',             'win32-dir-sep');
check(IO::Spec::Unix.curdir,   '.',              'unix-curdir');
check(IO::Spec::Unix.updir,    '..',             'unix-updir');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
