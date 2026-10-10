unit module DirStat::Tally;

#| The extension a file is counted under; files without one go to '(none)'.
sub kind(IO::Path:D $file --> Str:D) is export {
    $file.extension || '(none)'
}

#| Files and bytes per extension, largest first.
sub tally(@files --> List:D) is export {
    my %by-kind;
    for @files -> $file {
        my $kind = kind $file;
        %by-kind{$kind}<files>++;
        %by-kind{$kind}<bytes> += $file.s;
    }

    %by-kind.map({ %( kind => .key, |.value ) })
            .sort({ -$_<bytes>, $_<kind> })
            .List
}
