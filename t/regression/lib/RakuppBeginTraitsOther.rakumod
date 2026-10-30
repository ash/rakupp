# For t/regression/module-trait-exported-from-begin.raku: a trait exported
# the usual way, at the top level, that has to join the BEGIN-exported ones.
unit module RakuppBeginTraitsOther;
our @marked;
multi trait_mod:<is>(Method:D \m, :$rbt-other!) is export { @marked.push: m.name }
