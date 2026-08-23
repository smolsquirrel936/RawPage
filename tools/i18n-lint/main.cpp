// i18n-lint -- the third of the four enforcement gates (Stage 1 T6).
//
// Exit codes are part of the contract, because CI must tell a real finding from a broken
// invocation:
//     0  clean (orphan-key notes may still be printed -- they are S3 and never block)
//     1  findings                -> the build should fail
//     2  usage or I/O error      -> the gate itself did not run; do NOT read this as clean
//
// Like the other two it links nothing but the C++ standard library (D0-1), so `checks` still runs
// on the bare Linux `gates` runner with no Qt installed.

#include "tools/i18n-lint/I18nLint.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int usage(const char* argv0)
{
    std::cerr
        << "usage: " << argv0 << " --root <dir> [--rules <file>] [--locales <file>]\n"
        << "           [--actions <ActionId.def>] [--explain]\n"
        << "\n"
        << "  --root <dir>      repository root to scan\n"
        << "  --rules <file>    rule file (default: <root>/tools/i18n-lint/rules.json)\n"
        << "  --locales <file>  locale declaration (default: <root>/i18n/locales.json)\n"
        << "  --actions <file>  action table (default: <root>/core/cmd/ActionId.def)\n"
        << "  --explain         print the rules, the declared locales, and every live waiver\n"
        << "\n"
        << "  A DEFAULTED input that is absent is reported and its rules are skipped; an input\n"
        << "  named EXPLICITLY and absent is exit 2. A fixture tree legitimately has no catalogs;\n"
        << "  the real tree is always given all three, so an absence there cannot pass as clean.\n";
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path root;
    std::filesystem::path rulesPath;
    std::filesystem::path localesPath;
    std::filesystem::path actionDefPath;
    bool localesExplicit = false;
    bool actionsExplicit = false;
    bool wantExplain = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--root" && i + 1 < argc) {
            root = argv[++i];
        } else if (arg == "--rules" && i + 1 < argc) {
            rulesPath = argv[++i];
        } else if (arg == "--locales" && i + 1 < argc) {
            localesPath = argv[++i];
            localesExplicit = true;
        } else if (arg == "--actions" && i + 1 < argc) {
            actionDefPath = argv[++i];
            actionsExplicit = true;
        } else if (arg == "--explain") {
            wantExplain = true;
        } else if (arg == "-h" || arg == "--help") {
            return usage(argv[0]);
        } else {
            std::cerr << "i18n-lint: unrecognized argument '" << arg << "'\n";
            return usage(argv[0]);
        }
    }

    if (root.empty()) {
        std::cerr << "i18n-lint: --root is required\n";
        return usage(argv[0]);
    }
    if (rulesPath.empty()) rulesPath = root / "tools" / "i18n-lint" / "rules.json";
    if (localesPath.empty()) localesPath = root / "i18n" / "locales.json";
    if (actionDefPath.empty()) actionDefPath = root / "core" / "cmd" / "ActionId.def";

    try {
        const rawpage::tools::Inputs inputs = rawpage::tools::load(
            root, rulesPath, localesPath, localesExplicit, actionDefPath, actionsExplicit);

        if (wantExplain) {
            rawpage::tools::explain(inputs, std::cout);
            return 0;
        }
        return rawpage::tools::run(inputs, std::cout);
    } catch (const std::exception& error) {
        // Exit 2, never 1: a gate that could not run is not a gate that passed.
        std::cerr << "i18n-lint: " << error.what() << "\n";
        return 2;
    }
}
