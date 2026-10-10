# t/regression/export-sub-scope.raku: a `sub EXPORT` written after
# `unit module` is the package's, and Rakudo never calls it on `use`.
unit module RakuppPkgExport;
sub EXPORT { Map.new("&pkg-export-hello" => sub { "hi" }) }
our sub there { "there" }
