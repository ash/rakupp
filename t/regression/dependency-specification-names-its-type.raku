# `CompUnit::DependencySpecification.new(:short-name<Foo>)` answered an object
# whose `.^name` was the engine's internal tag, `DependencySpec`, so every
# `$spec ~~ CompUnit::DependencySpecification` was False. Identity::Utils
# asserts exactly that, and so do PURL, SBOM::Raku, MCP and Pod::TreeWalker.
use Test;
plan 3;

my $spec = CompUnit::DependencySpecification.new(short-name => 'JSON::Fast');

is $spec.short-name, 'JSON::Fast',                      'the descriptor keeps its name';
is $spec.^name, 'CompUnit::DependencySpecification',    '…and reports the type it is';
ok $spec ~~ CompUnit::DependencySpecification,          '…and type-checks as one';
