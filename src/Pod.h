#pragma once
#include "Value.h"
#include <string>
#include <vector>

namespace rakupp {
// Parse every POD block in `src` into the `$=pod` DOM: a list of Pod::Block
// values (VT::Hash, hashKind "Pod") with a "podclass"/"name"/"level"/"config"/
// "contents" shape. Delimited (=begin/=end), paragraph (=for), and abbreviated
// (=head1 …) forms; nested blocks; whitespace-collapsed paragraphs.
ValueList parsePod(const std::string& src);

// Pod::To::Text's `pod2text`, in the shape Rakudo's core module produces:
// paragraphs flat, headings indented two spaces per level below the first,
// items bulleted, code indented four, a named block prefixed with its own
// name (except `pod`), comments empty, and sibling blocks separated by a
// blank line.
std::string pod2text(const Value& v);
}
