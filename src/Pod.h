#pragma once
#include "Value.h"
#include <string>
#include <vector>

namespace rakupp {
// Parse every POD block in `src` into the `$=pod` DOM: a list of Pod::Block
// values (VT::Hash, hashKind "Pod") with a "podclass"/"name"/"level"/"config"/
// "contents" shape. Delimited (=begin/=end), paragraph (=for), and abbreviated
// (=head1 …) forms; nested blocks; whitespace-collapsed paragraphs.
// strict: a malformed table (empty, consecutive separators, mixed column
// separators) is an error, as it is in Rakudo. EVAL parses strictly; the
// main program's pod scan is textual and cannot tell a heredoc from pod, so
// it stays lenient.
ValueList parsePod(const std::string& src, bool strict = false);

// Pod::To::Text's `pod2text`, in the shape Rakudo's core module produces:
// paragraphs flat, headings indented two spaces per level below the first,
// items bulleted, code indented four, a named block prefixed with its own
// name (except `pod`), comments empty, and sibling blocks separated by a
// blank line.
std::string pod2text(const Value& v);
}
