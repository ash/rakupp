unit module Stowaway;

# Present in lib/ but never declared in the META6.json `provides` map.
sub stowaway(--> Str) is export { 'stowaway' }
