# Regression: IO::Spec::Unix gaps, the Versioning setters, enum-value
# introspection, and a filetest adverb inside a junction.
#   * `IO::Spec::Unix.path` never tagged its result a Seq; the Win32 branch
#     already did, so the two spellings rendered differently. Both exits (the
#     empty-PATH one included) tag it now.
#   * `.split('')` — the empty path is the one input whose dirname is "" and not
#     ".". With p empty the no-separator arm fires and hard-codes the curdir
#     default, which is right for `foo` and wrong for ``.
#   * `.splitpath(:nofile)` was silently discarded: Win32 and Cygwin parse the
#     flag, the plain-Unix branch never looked at its argument list at all.
#   * `.^set_ver`/`.^set_auth`/`.^set_api` did not exist — only the getters did,
#     while the howOps table already forwarded `$t.HOW.set_ver($t, v)` to them.
#     `:ver<0.0.1>` stores a BARE "0.0.1" and the getter re-adds the `v`, so a
#     `v0.0.1` literal argument has to be stripped or it comes back `vv0.0.1`.
#   * `.enums` was implemented for the enum TYPE only, so `Mass.enums` worked and
#     `g.enums` fell off the ladder. An enum value now forwards type-level
#     queries to its type object — guarded on the type object itself (which
#     carries enumType too) so the forward cannot recurse.
#   * `$path.IO ~~ :d & :x` — the filetest-adverb rule lived only in the direct
#     `~~` arm, and the junction loop below it re-matched each eigenstate with
#     the GENERIC smartmatch, which knows nothing about IO or Pairs. Every Pair
#     eigenstate answered False, so the whole junction did.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# IO::Spec::Unix
%*ENV<PATH> = 'foo:bar';
check(IO::Spec::Unix.path.raku, '("foo", "bar").Seq', 'path answers a Seq');
check(IO::Spec::Unix.split('').raku,     'IO::Path::Parts.new("","","")',    'the empty path has no dirname');
check(IO::Spec::Unix.split('foo').raku,  'IO::Path::Parts.new("",".","foo")', 'a bare name still gets curdir');
check(IO::Spec::Unix.split('/a/b').raku, 'IO::Path::Parts.new("","/a","b")',  'and an ordinary path is unchanged');
check(IO::Spec::Unix.splitpath('C:\foo/bar.txt', :nofile).raku,
      '("", "C:\\\\foo/bar.txt", "")', ':nofile makes the whole path the directory');
check(IO::Spec::Unix.splitpath('C:\foo/bar.txt').raku,
      '("", "C:\\\\foo/", "bar.txt")', 'and without it the split is unchanged');

# the Versioning setters
class RgV { }
RgV.^set_ver: v0.0.1;
check(RgV.^ver.gist, 'v0.0.1', 'set_ver, with the leading v stripped once');
class RgV2 { }
RgV2.^set_auth('github:x');
check(RgV2.^auth, 'github:x', 'set_auth');
class RgV3 { }
RgV3.^set_api('1');
check(RgV3.^api, '1', 'set_api');
class RgV4:ver<0.0.9> { }
check(RgV4.^ver.gist, 'v0.0.9', 'a declared :ver still reads back');

# enum-value introspection
enum RgMass ( mg => 1/1000, g => 1/1, kg => 1000/1 );
enum RgSize <small medium large>;
check(g.enums.gist,      'Map.new((g => 1, kg => 1000, mg => 0.001))', 'a value forwards .enums to its type');
check(RgMass.enums.gist, 'Map.new((g => 1, kg => 1000, mg => 0.001))', 'and the type still answers');
check(medium.enums.gist, 'Map.new((large => 2, medium => 1, small => 0))', 'for a plain enum too');
check(medium.key,        'medium', '.key is unaffected');
check(medium.value,      '1',      'and .value');

# a filetest adverb inside a junction
check(('/tmp'.IO ~~ :d).gist,      'True',  'a bare filetest');
check(('/tmp'.IO ~~ :f).gist,      'False', 'a false one');
check(('/tmp'.IO ~~ :d & :x).gist, 'True',  'all of two filetests');
check(('/tmp'.IO ~~ :d | :f).gist, 'True',  'any of two');
check(('/tmp'.IO ~~ :f & :d).gist, 'False', 'and one that fails');
check((5 ~~ (Int & Cool)).gist,    'True',  'an ordinary type junction still autothreads');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
