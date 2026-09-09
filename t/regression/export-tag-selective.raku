# Regression: `use Mod :sometag` imported every DEFAULT-tagged name as well as
# the requested one, so a module could not keep half its surface out of a
# caller that asked for the other half.
#
# Naming a tag REPLACES the default set rather than adding to it. `:MANDATORY`
# is exported whatever was asked for — that is what the tag means — but
# `:DEFAULT` is not a free pass: `use Mod :beta` leaves `is export(:DEFAULT)`
# undeclared in the importer, and `is export(:DEFAULT, :beta)` comes in on the
# strength of `:beta` alone. Treating DEFAULT as always-publishing handed a
# selective importer most of the module, which is precisely what its author
# reached for tags to avoid.
#
# The table below is Rakudo's, measured form by form (rakudo 2026.08). The
# rows that used to differ are `:beta`, `:gamma` and `:beta, :gamma` — every
# one of them on the `dflt`/`both` columns.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("exporttag-{$*PID}");
$dir.mkdir;
$dir.add('TagMx.rakumod').spurt: q:to/MOD/;
    unit module TagMx;
    sub plain()  is export                    { "p" }
    sub dflt()   is export(:DEFAULT)          { "d" }
    sub both()   is export(:DEFAULT, :beta)   { "b" }
    sub only()   is export(:beta)             { "o" }
    sub other()  is export(:gamma)            { "g" }
    sub mand()   is export(:MANDATORY)        { "m" }
    MOD

# One child per import form; it prints Y/- per name, in the order below.
my @names = <plain dflt both only other mand>;
sub visible(Str $form --> Str) {
    # The child prints one Y/- per name, in @names order. Built as ONE mapped
    # join rather than a chain of `~`-ed ternaries: without parens around each
    # `?? !!` that chain reassociates and the child prints a single character,
    # which reads as every row failing at once.
    my $code = "my @N = <{@names.join(' ')}>; use TagMx $form; "
             ~ 'print @N.map({ (::("&" ~ $_) ~~ Callable) ?? "Y" !! "-" }).join';
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $out
}

#                    plain dflt both only other mand
my %want =
    ''              => 'YYY--Y',   # no tags: the default set, plus MANDATORY
    ':DEFAULT'      => 'YYY--Y',   # asking for DEFAULT by name is the same
    ':beta'         => '--YY-Y',   # `both` on :beta, `dflt` withheld
    ':gamma'        => '----YY',   # nothing of :beta's, nothing default
    ':beta, :gamma' => '--YYYY',   # a union of the two, still no default
    ':ALL'          => 'YYYYYY',   # the catch-all
    ':MANDATORY'    => '----'  ~ '-Y',
;

for %want.keys.sort -> $form {
    check visible($form), %want{$form}, "use TagMx $form";
}

$dir.add('TagMx.rakumod').unlink;
sub nuke(IO::Path $p) {
    if $p.d { nuke($_) for $p.dir; try $p.rmdir }
    else { try $p.unlink }
}
nuke($dir);

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say 'PASS';
