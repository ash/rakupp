# BROKE: leg 22 — X::Package::Stubbed treated `use RoleB; role RoleB {...}`
# as a never-completed stub, but a stub naming a use'd module is a legal
# redeclaration hint (S11-modules/nested.t -> GONE, caught by gate av1).
# FIXED: same leg — stubs whose name matches a use'd module are exempt.
use lib $?FILE.IO.parent.parent.add('fixtures').Str;
use StubbedRole;
role StubbedRole {...}
class C does StubbedRole {}
die 'stub-after-use broken' unless so C ~~ StubbedRole;
say 'PASS';
