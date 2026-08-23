// The source side of i18n-lint: user-facing literals, catalog-key references, and waivers.
//
// ---------------------------------------------------------------------------------------------
// WHY THE RULE IS SINK-DRIVEN RATHER THAN LITERAL-DRIVEN.
//
// "A string literal that reaches a user" is the requirement. Flagging every string literal would
// flag object names, settings paths, catalog keys, format specifiers, file extensions and log text
// -- and a rule that fires on all of those is a rule people waive everywhere, which kills it inside
// a week (this stage's own risk register says so). So the scan looks for the SINKS: the calls that
// put text in front of a person. A literal reaching one of those, not wrapped in `tr()`, is the
// defect.
//
// The cost is that a literal handed to a user through a helper this file does not know about is
// missed. That is a false NEGATIVE, and the honest response is that the sink list is data
// (`rules.json`), grows as `ui/` grows, and is the first thing to check when a stray English string
// appears in a zh-TW screenshot.
//
// It is line-oriented for the same reason layer-lint is: this must run before anything is compiled,
// with no compile database and no Qt.

#ifndef RAWPAGE_I18N_LINT_SOURCESCAN_H
#define RAWPAGE_I18N_LINT_SOURCESCAN_H

#include "tools/i18n-lint/I18nRules.h"

#include <filesystem>
#include <string>
#include <vector>

namespace rawpage::tools {

struct LiteralHit {
    std::string file;
    int         line = 0;
    std::string sink;        // the call the literal reaches the user through
    std::string literal;     // the offending text, as written
};

struct KeyReference {
    std::string file;
    int         line = 0;
    std::string key;         // a catalog-key-shaped literal, e.g. "error.settings.write"
};

struct WaiverHit {
    std::string file;
    int         line = 0;
    std::string kind;
    std::string reason;
    bool        applied = false;   // did it actually silence a hit on its line
};

struct SourceScanResult {
    std::vector<LiteralHit>   literals;      // survivors: unwrapped AND unwaived
    std::vector<KeyReference> keyReferences;
    std::vector<WaiverHit>    waivers;
    std::size_t               filesScanned = 0;
    std::size_t               literalsConsidered = 0;
};

// Walks `root`, scanning the paths the rule set names. Throws std::runtime_error if root is not a
// directory.
SourceScanResult scanSources(const std::filesystem::path& root, const I18nRules& rules);

// Every `action.*` catalog key declared in an ActionId.def, in declaration order. Reading the .def
// as TEXT is deliberate and is written into the .def's own rule 3: the catalog key column exists as
// a literal column precisely so this gate can read it without a compiler.
std::vector<std::string> actionLabelKeys(const std::filesystem::path& actionDefPath);

} // namespace rawpage::tools

#endif // RAWPAGE_I18N_LINT_SOURCESCAN_H
