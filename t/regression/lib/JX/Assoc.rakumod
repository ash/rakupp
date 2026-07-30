unit class JX::Assoc;
use JX::Attr;
also does JX::Attr;
method who { $!declarant.^name }
