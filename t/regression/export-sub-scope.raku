# Regression, 2026-10-10: a `sub EXPORT` written after `unit module Foo` is
# Foo::EXPORT, a sub of the package, and Rakudo 2026.09 does not call it on
# `use` — only the compunit's own EXPORT (written before the `unit`
# declaration, or in a file with none) is the export protocol. rakupp called
# whichever `&EXPORT` the module's scope held.
use Test;
use lib $?FILE.IO.parent.add('lib').Str;
plan 3;

use RakuppPkgExport;
nok (try EVAL 'pkg-export-hello()'), 'a package-scoped EXPORT is not called';
is RakuppPkgExport::there(), 'there', '…and the module loads';
use RakuppUnitExport;
is unit-export-hello(), 'hello', 'the compunit EXPORT is';
