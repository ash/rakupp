# Exercised by every compile mode (--bundle / --aot / --exe) with its module
# tree DELETED, so each binary has to carry what it uses. Touches a sub, a
# class and an exported operator, across two levels of `use`.
use Outer;
say outer-value();
say Shape.new(n => 7).describe;
say 3 ⊕ 4;
