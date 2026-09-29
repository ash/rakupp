# Re-exports a dependency's routine under a tag built by hand — Cro::Uri's
# `package EXPORT::decode-percents`, in miniature.
use RakuppTagDep :tag-dp;
package EXPORT::tag-dp {
    our &tag-dp = &RakuppTagDep::tag-dp;
}
class RakuppTagTop { }
