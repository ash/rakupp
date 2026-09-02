# Regression: the engine gaps behind four top-100 roots that were each ONE
# fault from green (docs/dev/ecosystem/ECOSYSTEM-TOP100.md, 2026-09-02, the
# second sitting). Every expectation is the Rakudo 2026.08 answer, taken from
# probes run against both engines; each block names the dist that exposed it.
use NativeCall;

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }

# --- Text::MiscUtils: zip's single-argument rule --------------------------------
# `+lol`: ONE iterable argument is the list of lists, not itself a list.
# text-columns transposes its rows with `zip @fitted.map(*.[^$max])`, and the
# one-block case handed `("",)` to a Str:D parameter.
ok(zip(((1,2),(3,4))).raku eq '((1, 3), (2, 4))', 'zip of ONE list-of-lists zips its elements');
ok(zip((1,2,3)).raku eq '((1, 2, 3),)', 'zip of one flat list is one row');
my $one = ((("",),).Seq);
ok(zip($one).raku eq '(("",),)', 'zip of a one-list Seq is a one-element row');
ok(zip((1,2),(3,4)).raku eq '((1, 3), (2, 4))', 'two arguments zip as before');

# --- Color::Names: the dot-hyper SUBSCRIPT spelling ----------------------------
my @h = ({n => 1}, {n => 2});
ok((@h.»<n>).join(',') eq '1,2', '@a.»<key> is a hyper hash subscript');
ok((@h».<n>).join(',') eq '1,2', '…as @a».<key> already was');
my @l = ([5, 6], [7, 8]);
ok((@l.»[0]).join(',') eq '5,7', '@a.»[0] is a hyper positional subscript');
ok((@h.».elems).join(',') eq '1,1', '@a.».method still works');

# --- Pod::Load: formatting codes are Pod::FormattingCode nodes -----------------
=begin pod
Z<a comment> and C<some code> here, B<bold I<nested>> L<Raku|https://raku.org> X<<a>b>>
=end pod
my @c = $=pod[0].contents[0].contents;
ok(@c[0] ~~ Pod::FormattingCode && @c[0].type eq 'Z', 'Z<> is a FormattingCode of type Z');
ok(@c[0].contents[0] eq 'a comment', '…holding its text');
ok(@c[1] ~~ Str && @c[1] eq ' and ', 'text between codes stays a Str');
ok(@c[2].type eq 'C' && @c[2].contents[0] eq 'some code', 'C<> keeps its text verbatim');
ok(@c[4].type eq 'B', 'B<bold I<nested>> is a B');
ok(@c[4].contents[0] eq 'bold ' && @c[4].contents[1].type eq 'I'
   && @c[4].contents[1].contents[0] eq 'nested', '…whose contents nest an I');
ok(@c[6].type eq 'L' && @c[6].contents[0] eq 'Raku' && @c[6].meta[0] eq 'https://raku.org',
   'L<text|url> splits contents from meta');
ok(@c[8].type eq 'X' && @c[8].contents[0] eq 'a>b', '<<…>> lets a > through');
ok(@c[0].^name eq 'Pod::FormattingCode', '.^name names the class');
ok(@c.grep({ .?type ~~ 'C' }).elems == 1, '.?type on a Str element is Nil, not a death');

# --- Compress::Zlib: a Buf grown by index is shared storage for a native view --
# flush sizes its output with `$output-buf[1023] = 1`, then hands zlib a
# `nativecast(CArray[uint8], $buf)` view; the Z_FINISH trailer came back zeros.
my $b = buf8.new; $b[1023] = 1;
my $view = nativecast(CArray[uint8], $b);
$view[0] = 42; $view[5] = 99;
ok($b[0] == 42 && $b[5] == 99, 'a write through the view reaches an index-grown Buf');
ok($b.subbuf(0, 6).list eqv (42, 0, 0, 0, 0, 99), '…and subbuf reads those bytes');
ok($b.bytes == 1024 && $b[1023] == 1, 'the grow itself is intact');

# --- Pod::Load: `use lib` cannot be precompiled -------------------------------
# A file loaded THROUGH the precompilation store is a module to Rakudo, and a
# module may not `use lib`; the refusal reaches Pod::Load as SourceErrors.
my $f = $*TMPDIR.add("rakupp-uselib-{$*PID}.raku");
$f.spurt("use lib '.';\n=begin pod\nhi\n=end pod\n");
my $msg = '';
{
    my $repo = CompUnit::PrecompilationRepository::Default.new(:store(Nil));
    my $dep = CompUnit::PrecompilationDependency::File.new(
        :src($f.Str),
        :id(CompUnit::PrecompilationId.new-from-string($f.Str)),
        :spec(CompUnit::DependencySpecification.new(:short-name($f.Str))));
    try { $repo.try-load($dep); CATCH { default { $msg = .message } } }
}
$f.unlink;
ok($msg.contains("'use lib' cannot be precompiled"), 'try-load refuses a file that uses lib');
ok($msg.contains('lib'), '…with a message Pod::Load can match /lib/ against');
# …while a file WITHOUT it still loads
my $g = $*TMPDIR.add("rakupp-nolib-{$*PID}.raku");
$g.spurt("=begin pod\nhi\n=end pod\n");
my $loaded = False;
{
    my $repo = CompUnit::PrecompilationRepository::Default.new(:store(Nil));
    my $dep = CompUnit::PrecompilationDependency::File.new(
        :src($g.Str),
        :id(CompUnit::PrecompilationId.new-from-string($g.Str)),
        :spec(CompUnit::DependencySpecification.new(:short-name($g.Str))));
    my $h = $repo.try-load($dep);
    $loaded = $h.defined;
}
$g.unlink;
ok($loaded, 'a file without `use lib` loads through the same path');

say $fails ?? "FAIL ($fails)" !! "PASS";
