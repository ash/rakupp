# Fixture for t/regression/stub-role-after-use.raku — a module whose name a
# consuming file may re-stub (`role StubbedRole {...}`) without tripping
# X::Package::Stubbed (the S11-modules/nested.t pattern).
unit role StubbedRole;
method fixture-tag() { 'from-module' }
