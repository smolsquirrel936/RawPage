#ifndef RAWPAGE_I18N_LINT_I18NLINT_H
#define RAWPAGE_I18N_LINT_I18NLINT_H

#include "tools/i18n-lint/Catalog.h"
#include "tools/i18n-lint/I18nRules.h"
#include "tools/i18n-lint/SourceScan.h"

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawpage::tools {

struct Finding {
    std::string file;
    int         line = 0;
    std::string rule;      // "unwrapped-literal", "missing-key", "orphan-key", "action-label"
    std::string detail;
    std::string why;
    bool        blocking = true;   // orphan-key is S3: reported, never the reason a build fails
};

// Everything the gate looked at, so `--explain` and the summary line describe the same run.
struct Inputs {
    std::filesystem::path root;
    I18nRules             rules;
    std::optional<LocaleSet>          locales;
    std::filesystem::path             actionDefPath;
    bool                              actionDefPresent = false;
    SourceScanResult                  sources;
    std::vector<std::pair<LocaleDeclaration, Catalog>> catalogs;   // declared AND loadable
};

// Loads everything. Throws std::runtime_error -- which main() turns into exit 2 -- when an input
// that was named EXPLICITLY cannot be read. An input that was merely defaulted and is absent is
// recorded as absent instead, and reported: a gate that could not run must never look like a gate
// that passed, and it must equally never fail a fixture tree for lacking a catalog it was never
// given.
Inputs load(const std::filesystem::path& root, const std::filesystem::path& rulesPath,
            const std::filesystem::path& localesPath, bool localesExplicit,
            const std::filesystem::path& actionDefPath, bool actionDefExplicit);

std::vector<Finding> check(const Inputs& inputs);

// Prints findings and returns the process exit code: 0 clean, 1 blocking findings.
int run(const Inputs& inputs, std::ostream& out);

// --explain: the rules, the declared locales with their shipped state and reason, the waiver
// census, and the catalog census.
void explain(const Inputs& inputs, std::ostream& out);

} // namespace rawpage::tools

#endif // RAWPAGE_I18N_LINT_I18NLINT_H
