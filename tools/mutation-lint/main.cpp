// mutation-lint -- the fourth of the four enforcement gates (Stage 3 T6, decision D3-12).
//
// Exit codes are part of the contract, because CI must tell a real finding from a broken invocation:
//     0  clean
//     1  findings                -> the build should fail
//     2  usage or I/O error      -> the gate itself did not run; do NOT read this as clean
//
// Like the other three it links nothing but the C++ standard library (D0-1), so `checks` still runs
// on the bare Linux `gates` runner with no Qt installed.

#include "tools/mutation-lint/MutationLint.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int usage(const char* argv0)
{
    std::cerr << "usage: " << argv0 << " --root <dir> [--explain]\n"
              << "\n"
              << "  --root <dir>  repository root: reads core/cmd/Commands.def, core/cmd/commands/ and tests/\n"
              << "  --explain     print the inputs, the rules, what the gate cannot see, and every command\n"
              << "\n"
              << "  All three inputs are required. A root missing one is exit 2: a gate that could not\n"
              << "  read what it checks has not run.\n";
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path root;
    bool wantExplain = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--root" && i + 1 < argc) {
            root = argv[++i];
        } else if (arg == "--explain") {
            wantExplain = true;
        } else if (arg == "-h" || arg == "--help") {
            return usage(argv[0]);
        } else {
            std::cerr << "mutation-lint: unrecognized argument '" << arg << "'\n";
            return usage(argv[0]);
        }
    }
    if (root.empty()) {
        std::cerr << "mutation-lint: --root is required\n";
        return usage(argv[0]);
    }

    try {
        const rawpage::tools::MutationInputs inputs = rawpage::tools::loadMutationInputs(root);
        if (wantExplain) {
            rawpage::tools::explainMutationLint(inputs, std::cout);
            return 0;
        }
        return rawpage::tools::runMutationLint(inputs, std::cout);
    } catch (const std::exception& error) {
        // Exit 2, never 1: a gate that could not run is not a gate that passed.
        std::cerr << "mutation-lint: " << error.what() << "\n";
        return 2;
    }
}
