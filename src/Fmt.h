#pragma once
#include <string>

namespace rakupp {

// Why a run produced no output. Every one of these is a REFUSAL: `--fmt` emits
// the formatted text or nothing, never a best-effort half-format.
enum class FmtStatus {
    Ok,
    ParseError,       // the input does not parse — exit 3
    SemanticRefusal,  // formatting changed the program — exit 5, should never fire
    NotIdempotent,    // fmt(fmt(x)) != fmt(x) — exit 5, likewise
};

struct FmtResult {
    FmtStatus status = FmtStatus::Ok;
    std::string text;      // the formatted source (Ok only)
    bool changed = false;  // …and whether it differs from the input
};

// Format Raku source. The three gates in FMT-PLAN — parse, semantic,
// idempotence — are inside this call, not in the CLI, so every caller gets
// them.
FmtResult formatSource(const std::string& src);

// A unified diff of `before` -> `after`, for `--fmt --diff`. Built in rather
// than shelled out to `diff`/git: a formatter that needs another tool
// installed to explain itself is not one you put in CI.
std::string fmtUnifiedDiff(const std::string& name,
                           const std::string& before, const std::string& after);

} // namespace rakupp
