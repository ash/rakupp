# A unit class whose attribute is typed by an `our subset` it declares, with a
# default the subset's `where` accepts — URI's Scheme, in miniature.
unit class RakuppSubsetDefault;
our subset Tag of Str where /^ [ '' || <[a..z]>+ ] $/;
has Tag $.tag = '';
