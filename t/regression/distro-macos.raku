# Regression: $*DISTRO on macOS carries what Rakudo's Distro.rakumod fills in
# from `sw_vers` — .version is the product version (a Version), .release the
# build, .auth 'Apple Inc.', and .desc the marketing name that the system's own
# software-licence page opens with ("Tahoe 26"). rakupp answered its NAME for
# .desc, 0 for .version and the KERNEL release for .release, so Roast's
# `todo(…) if $*DISTRO.desc eq 'Sonoma' | 'Sequoia' | 'Tahoe 26'` guards
# never fired here. The expectations are read from the same sources at run
# time, so the file passes on any macOS — and under Rakudo, where they come
# from. Elsewhere there is nothing to compare and it passes trivially.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

if $*DISTRO.name eq 'macos' {
    my %sw;
    for shell('sw_vers', :out, :err).out.slurp(:close).lines -> $line {
        my $c = $line.index(':') // next;
        %sw{$line.substr(0, $c).trim} = $line.substr($c + 1).trim;
    }
    check($*DISTRO.version.Str,  %sw<ProductVersion>,       '.version is the product version');
    check($*DISTRO.version.raku, 'v' ~ %sw<ProductVersion>, 'and a Version, not a Str');
    check($*DISTRO.release,      %sw<BuildVersion>,         '.release is the build');
    check($*DISTRO.auth,         'Apple Inc.',              '.auth');
    check($*DISTRO.gist,         "macos (%sw<ProductVersion>)", '.gist is name (version)');
    check($*DISTRO.Str,          'macos',                   '.Str is the name');
    check($*DISTRO.is-win.gist,  'False',                   'not Windows');
    check($*DISTRO.path-sep,     ':',                       'PATH separator');
    # Rakudo's rule for .desc: a static table up to El Capitan, else the phrase
    # the licence page opens with; mirrored here with plain string ops
    my %names = <10.0 Cheetah 10.1 Puma 10.2 Jaguar 10.3 Panther 10.4 Tiger 10.5 Leopard
                 10.6 SnowLeopard 10.7 Lion 10.8 MountainLion 10.9 Mavericks 10.10 Yosemite 10.11 ElCapitan>;
    my $want-desc = do if %names{%sw<ProductVersion>}:exists { $*DISTRO.desc }   # two-word names lost their space above; not checkable
    else {
        my $phrase = 'SOFTWARE LICENSE AGREEMENT FOR macOS ';
        my $html = '/System/Library/CoreServices/Setup Assistant.app/Contents/Resources/en.lproj/OSXSoftwareLicense.html'.IO.slurp;
        with $html.index($phrase) -> $at {
            my $rest = $html.substr($at + $phrase.chars);
            $rest.substr(0, $rest.index('<') // $rest.chars);
        }
        else { '<unknown>' }
    }
    check($*DISTRO.desc, $want-desc, '.desc is the marketing name from the licence page');
    check(($*DISTRO.desc eq 'macos').gist, 'False', 'and not merely the name');
}
else { note "not macOS: nothing to compare against" }

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
