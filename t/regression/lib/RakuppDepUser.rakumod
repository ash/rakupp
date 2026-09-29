# A module whose routine names a class from a module IT loaded — JSON::Tiny's
# from-json and JSON::Tiny::Actions, in miniature.
unit module RakuppDepUser;
use RakuppDepHelper;
sub dep-greet is export { RakuppDepHelper.new.greet }
