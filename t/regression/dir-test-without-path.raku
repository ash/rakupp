# Regression: issue #62 — `dir(test => /csv$/)` answered an empty list. The
# sub form took its first argument as the path whether or not that argument
# was the `:test` Pair, so a named-only call opened a directory spelled
# "test\t…", failed, and reported it empty. The path is the first POSITIONAL
# and defaults to `.`, as in Rakudo's `sub dir(Cool $path = '.', Mu :$test)`.
#
# The listing was also a second copy of IO::Path.dir with rules of its own
# (`./x` entries, `.`/`..` hidden even under an explicit :test, a Block
# matcher died on ACCEPTS). The sub is now the method, so the two agree —
# including on a directory that cannot be read, which is an X::IO::Dir and
# not an empty list: that silence is what hid the bug.
# Contract: exit 0 + last line PASS.
my @fail;
my $dir = $*TMPDIR.add("rakupp-dir-62-{$*PID}");
$dir.mkdir;
$dir.add($_).spurt('') for <a.csv b.csv c.txt>;
$dir.add('sub').mkdir;

sub caught(&code) { my $e; { code(); CATCH { default { $e = $_ } } }; $e }

my $save = $*CWD;
chdir $dir;

# the issue itself: a named-only call lists the current directory
@fail.push('dir(test => …) lists .') unless dir(test => /csv$/).map(*.basename).sort.join(' ') eq 'a.csv b.csv';
@fail.push('dir(:test(…))')          unless dir(:test(/csv$/)).elems == 2;
@fail.push('dir(".", :test)')        unless dir('.', test => /csv$/).elems == 2;

# every kind of matcher the method form takes: Block, Junction, Str
@fail.push('Block test')  unless dir(test => { .ends-with('.txt') }).elems == 1;
@fail.push('none() test') unless dir(test => none('.', '..', 'sub')).elems == 3;
@fail.push('Str test')    unless dir(test => 'c.txt').map(*.basename).join(' ') eq 'c.txt';

# the default :test hides `.` and `..`; an explicit one lets them through
@fail.push('. and .. hidden')        if     dir().grep(*.basename eq '.' | '..');
@fail.push('. and .. under :test(*)') unless dir(test => *).grep(*.basename eq '.' | '..').elems == 2;

# entries of the current directory are spelled bare, not `./x`
@fail.push('bare entries')      if     dir().grep(*.Str.starts-with('./'));
@fail.push('dir("sub") prefix') unless dir('sub', test => *).map(*.Str).sort.join(' ') eq 'sub/. sub/..';

# entries are IO::Paths based where the call was made
@fail.push('IO::Path entries') unless dir().all ~~ IO::Path;
@fail.push('captured CWD')     unless dir()».CWD.unique.join(',') eq $dir.Str;

chdir $save;

# inside a :test callback $*CWD is the path's OWN :CWD (S32-io/dir.t), and
# the caller's comes back afterwards
my $seen = '';
dir(IO::Path.new($dir.Str, :CWD($dir.Str)), :test{ $seen = $*CWD.Str; True }).elems;
@fail.push('$*CWD inside :test') unless $seen eq $dir.Str;
@fail.push('$*CWD restored')     unless $*CWD.Str eq $save.Str;

# a directory that cannot be read is an error, not an empty list
my $ex = caught({ dir($dir.add('nope')) });
@fail.push('missing dir throws') unless $ex ~~ X::IO::Dir;
$ex = caught({ $dir.add('c.txt').dir });
@fail.push('non-dir throws')           unless $ex ~~ X::IO::Dir;
@fail.push('X::IO::Dir names the path') unless $ex.defined && $ex.message.contains('c.txt');

$dir.add($_).unlink for <a.csv b.csv c.txt>;
$dir.add('sub').rmdir;
$dir.rmdir;

if @fail { say "FAIL: {@fail.join(', ')}"; exit 1 }
say 'PASS';
