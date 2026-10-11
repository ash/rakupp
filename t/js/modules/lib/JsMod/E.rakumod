# Exports through `sub EXPORT`, the way Math::Wavelet does: a sub of its own,
# and one re-exported from a module it uses.
use JsMod::A;
sub listing(--> List) { (1, 2, 3).List }
sub EXPORT {
    Map.new: ('&listing' => &listing, '&f' => &f)
}
