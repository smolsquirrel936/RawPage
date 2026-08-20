// layer-lint -- the first of the four enforcement gates.
//
// Exit codes are part of the contract, because CI must tell a real violation from a
// broken invocation:
//     0  clean
//     1  violations found          -> the build should fail
//     2  usage or I/O error        -> the gate itself did not run; do NOT read this as clean

#include "tools/layer-lint/IncludeGraph.h"
#include "tools/layer-lint/LayerLint.h"
#include "tools/layer-lint/RuleSet.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int usage(const char* argv0) {
    std::cerr
        << "usage: " << argv0 << " --root <dir> [--rules <file>] [--explain[=dot]]\n"
        << "\n"
        << "  --root <dir>     repository root to scan\n"
        << "  --rules <file>   rules file (default: <root>/tools/layer-lint/rules.json)\n"
        << "  --explain        print the include graph, the active rules, and every live escape\n"
        << "  --explain=dot    the same graph as Graphviz DOT\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path root;
    std::filesystem::path rulesPath;
    bool wantExplain = false;
    bool explainAsDot = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--root" && i + 1 < argc) {
            root = argv[++i];
        } else if (arg == "--rules" && i + 1 < argc) {
            rulesPath = argv[++i];
        } else if (arg == "--explain") {
            wantExplain = true;
        } else if (arg == "--explain=dot") {
            wantExplain = true;
            explainAsDot = true;
        } else if (arg == "-h" || arg == "--help") {
            return usage(argv[0]);
        } else {
            std::cerr << "layer-lint: unrecognized argument '" << arg << "'\n";
            return usage(argv[0]);
        }
    }

    if (root.empty()) {
        std::cerr << "layer-lint: --root is required\n";
        return usage(argv[0]);
    }
    if (rulesPath.empty()) {
        rulesPath = root / "tools" / "layer-lint" / "rules.json";
    }

    try {
        const rawpage::tools::RuleSet rules = rawpage::tools::RuleSet::loadFromFile(rulesPath);
        const rawpage::tools::IncludeGraph graph = rawpage::tools::IncludeGraph::scan(root, rules);

        if (wantExplain) {
            rawpage::tools::explain(graph, rules, std::cout, explainAsDot);
            return 0;
        }
        return rawpage::tools::run(graph, rules);
    } catch (const std::exception& error) {
        // Exit 2, never 1: a gate that could not run is not a gate that passed, and CI
        // has to be able to tell those apart.
        std::cerr << "layer-lint: " << error.what() << "\n";
        return 2;
    }
}
