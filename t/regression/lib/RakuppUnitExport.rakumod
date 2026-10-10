# …and one written BEFORE it is the compunit's, which `use` calls.
sub EXPORT { Map.new("&unit-export-hello" => sub { "hello" }) }
unit module RakuppUnitExport;
