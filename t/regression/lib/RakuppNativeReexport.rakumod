# NativeLibs' EXPORT, line for line: find the `is native` candidate among
# `&trait_mod:<is>`'s and hand its dispatcher to the importer. Companion to
# t/regression/issue-211-trait-is-candidates.raku.
use NativeCall;
sub EXPORT(|) {
    my $exp = &trait_mod:<is>.candidates.first: { .signature ~~ :(Routine, :$native!) };
    Map.new('&trait_mod:<is>' => $exp.dispatcher)
}
unit module RakuppNativeReexport;
