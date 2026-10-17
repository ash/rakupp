#pragma once
// SLIM-PLAN P4: the feature scan. Walks a parsed program plus every module
// embedded alongside it and answers, per cuttable feature, "can any site
// reach this?" — plus the force-full triggers, the constructs that mean the
// program can run code the scan never saw. `--slim=auto` cuts only what the
// scan PROVES unreachable and keeps everything when a trigger fires;
// `--slim=max` trusts the per-feature evidence and ignores the triggers.
// The default answer to uncertainty is always "keep".

#include "Ast.h"
#include <set>
#include <string>
#include <vector>

namespace rakupp {

struct BundledModule;

struct SlimScanResult {
    // Index-aligned with main.cpp's kSlimFeatures:
    // 0 unicode-names, 1 unicode-collation, 2 unicode-props, 3 eval.
    bool used[4] = {false, false, false, false};
    // Human-readable force-full reasons, empty when the scan saw everything.
    std::vector<std::string> triggers;
    // Every use-site, in walk order — what forced a feature to be kept, and
    // where. `--slim=list` shows the first per feature; `--slim=why:FEAT`
    // shows them all. `where` is "" for the mainline, else the module name.
    struct Site { int feat; std::string what; std::string where; int line; };
    std::vector<Site> sites;
    // The builtin Raku (BuiltinRakuSrc.h) a program may reach through a name,
    // by the table's `needs` tag ("cathandle", "notification"): a compile that
    // cuts `eval` carries those pre-parsed, beside the always-carried ones.
    std::set<std::string> builtinTags;
};

// `engineAnswered`: the modules collectModuleGraph left to the engine's own
// builtins (Data::Native and its siblings) — `use`d, not embedded, and not a
// disk load either.
SlimScanResult slimScan(const Program& prog, const std::vector<BundledModule>& mods,
                        const std::set<std::string>& engineAnswered);

}
