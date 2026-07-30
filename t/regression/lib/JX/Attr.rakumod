unit role JX::Attr;
use JX::Descriptor;
also does JX::Descriptor;
method via-method { self.declarant.^name }
