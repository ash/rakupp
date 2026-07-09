#pragma once
#include "Value.h"
#include <string>
#include <vector>

namespace rakupp {
// Parse every POD block in `src` into the `$=pod` DOM: a list of Pod::Block
// values (VT::Hash, hashKind "Pod") with a "podclass"/"name"/"level"/"config"/
// "contents" shape. Delimited (=begin/=end), paragraph (=for), and abbreviated
// (=head1 …) forms; nested blocks; whitespace-collapsed paragraphs.
std::vector<Value> parsePod(const std::string& src);
}
