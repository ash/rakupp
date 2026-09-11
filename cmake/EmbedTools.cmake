# The Raku programs `rakupp install`, `rakupp doc` and `rakupp upgrade`
# dispatch to, compiled into the CLI as byte arrays. A binary on its own —
# copied into a container, unpacked as a bare rakupp.exe, installed by a route
# that dropped libexec/ — runs the same tools as a checkout, because it carries
# them.
#
# Run as a SCRIPT (cmake -P) from a custom command, with the five inputs as
# DEPENDS, so editing tools/install.raku or a guide regenerates and rebuilds
# exactly one translation unit. Nothing generated is checked in: the sources
# stay ordinary .raku and .md files, editable, lintable and directly runnable,
# and there is never a second copy in git to keep in step.
#
# It is CMake rather than Raku on purpose. A Raku generator would need a
# working rakupp to build rakupp, which is why the earlier version had to
# commit its output. `file(READ ... HEX)` needs nothing but CMake.
#
# BYTES, not string literals: MSVC caps one literal at 16 KB, so text this size
# would have to be split into chunks, and a chunk boundary is a thing to get
# wrong. A byte array has no cap, no escaping, and no delimiter that the
# content could accidentally contain. src/AstEmit.cpp emits ASTs the same way,
# for the same reason.
#
# Expects: SRC_DIR (repo root), OUT_FILE (the .cpp to write).

# Read one file and append it to `out` as a byte array plus its length.
function(embed_file out_var sym path)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "EmbedTools: missing input ${path}")
  endif()
  file(READ "${path}" hex HEX)
  string(LENGTH "${hex}" hexlen)
  math(EXPR nbytes "${hexlen} / 2")
  # "0x" before every pair, "," after: one string(REGEX REPLACE) over the whole
  # file, because a per-byte loop over 85 KB is minutes of CMake.
  string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
  # Wrap so the generated file stays openable in an editor.
  string(REGEX REPLACE "(([^,]*,){16})" "\\1\n  " bytes "${bytes}")
  set(${out_var}
      "static const unsigned char ${sym}[] = {\n  ${bytes}\n};\nstatic const size_t ${sym}Len = ${nbytes};\n"
      PARENT_SCOPE)
endfunction()

embed_file(BLOB_INSTALL  kInstall "${SRC_DIR}/tools/install.raku")
embed_file(BLOB_DOC      kDoc     "${SRC_DIR}/tools/doc.raku")
embed_file(BLOB_UPGRADE  kUpgrade "${SRC_DIR}/tools/upgrade.raku")
embed_file(BLOB_REFERENCE kRef    "${SRC_DIR}/docs/guide/REFERENCE.md")
embed_file(BLOB_FEATURES kFeat    "${SRC_DIR}/docs/guide/FEATURES.md")

# The fixed half of the output. docToolSource() splices the two guides into
# doc.raku's `my %DOCS;` declaration, so `rakupp doc` answers from a lone
# binary; doc.raku reads that hash before it goes looking on disk, and a
# checkout leaves it empty. The splice is C++ rather than CMake because CMake
# has no business quoting Markdown. (The block is one QUOTED argument, so
# its semicolons are literal and need no escaping — an escaped one would be
# emitted as a backslash and would not compile.)
set(EPILOGUE "
std::string installerSource() {
    return std::string(reinterpret_cast<const char*>(kInstall), kInstallLen);
}

std::string upgradeToolSource() {
    return std::string(reinterpret_cast<const char*>(kUpgrade), kUpgradeLen);
}

// A guide as a Raku heredoc body. Q:to/…/ interpolates nothing and unescapes
// nothing, so the Markdown goes in as it is; the only text that could end it
// early is a line spelled exactly like the terminator, which
// t/install/run.raku checks for.
static std::string heredoc(const char* name, const unsigned char* p, size_t n) {
    std::string body(reinterpret_cast<const char*>(p), n);
    if (body.empty() || body.back() != '\\n') body += '\\n';
    return std::string(\"%DOCS{'\") + name + \"'} = Q:to/RAKUPP-EMBED-END/;\\n\"
         + body + \"RAKUPP-EMBED-END\\n\";
}

std::string docToolSource() {
    std::string s(reinterpret_cast<const char*>(kDoc), kDocLen);
    static const char kMarker[] = \"my %DOCS;\\n\";
    size_t at = s.find(kMarker);
    if (at == std::string::npos) return s;   // no declaration to fill: run as-is
    std::string fill = kMarker;
    fill += heredoc(\"REFERENCE.md\", kRef, kRefLen);
    fill += heredoc(\"FEATURES.md\", kFeat, kFeatLen);
    s.replace(at, sizeof(kMarker) - 1, fill);
    return s;
}
} // namespace rakupp
")

set(GENERATED "\
// GENERATED at build time by cmake/EmbedTools.cmake from tools/install.raku,
// tools/doc.raku, tools/upgrade.raku and docs/guide/{REFERENCE,FEATURES}.md —
// DO NOT EDIT, and do not commit: this file lives in the build tree so those
// five stay the only copy of their own contents.
#include \"EmbeddedTools.h\"
#include <cstddef>
namespace rakupp {
${BLOB_INSTALL}
${BLOB_DOC}
${BLOB_UPGRADE}
${BLOB_REFERENCE}
${BLOB_FEATURES}
${EPILOGUE}")

# copy_if_different, like BuildInfo.cmake: touching a guide without changing it
# must not recompile anything.
file(WRITE "${OUT_FILE}.tmp" "${GENERATED}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${OUT_FILE}.tmp" "${OUT_FILE}")
file(REMOVE "${OUT_FILE}.tmp")
