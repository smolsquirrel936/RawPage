// license-audit -- the gate that mechanically enforces D4 ("no GPL or AGPL dependency may
// ever enter the tree") and the LGPL/Runtime-Library-Exception obligations around it.
//
// Exit codes, as for layer-lint:
//     0  clean
//     1  findings                   -> the build should fail
//     2  usage or I/O error         -> the gate did not run; NOT the same as clean

#include "tools/license-audit/Inventory.h"
#include "tools/license-audit/LicenseAudit.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int usage(const char* argv0) {
    std::cerr
        << "usage: " << argv0 << " --manifest <vcpkg.json> --spdx-map <spdx-map.json>\n"
        << "           [--vcpkg-installed <dir>] [--qt-modules <file>]\n"
        << "           [--compiler-plugins <file>] [--emit <THIRD-PARTY.md>]\n"
        << "\n"
        << "  --vcpkg-installed  an installed tree; supplies exact versions from vcpkg's SBOM.\n"
        << "                     Optional: the audit must run with no vcpkg at all, because the\n"
        << "                     CI gates job has none.\n"
        << "  --qt-modules       qt-modules.txt, written at configure time from the real link\n"
        << "                     closure. This is what catches a Qt6::Charts link.\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    recto::tools::InventoryInputs inputs;
    std::filesystem::path pluginList;
    std::filesystem::path emitPath;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::filesystem::path& target) {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + arg);
            target = argv[++i];
        };
        try {
            if (arg == "--manifest") next(inputs.manifestPath);
            else if (arg == "--spdx-map") next(inputs.spdxMapPath);
            else if (arg == "--vcpkg-installed") next(inputs.vcpkgInstalledDir);
            else if (arg == "--qt-modules") next(inputs.qtModulesPath);
            else if (arg == "--compiler-plugins") next(pluginList);
            else if (arg == "--emit") next(emitPath);
            else if (arg == "--feature") {
                if (i + 1 >= argc) throw std::runtime_error("missing value for --feature");
                inputs.activeFeatures.emplace_back(argv[++i]);
            }
            else if (arg == "-h" || arg == "--help") return usage(argv[0]);
            else {
                std::cerr << "license-audit: unrecognized argument '" << arg << "'\n";
                return usage(argv[0]);
            }
        } catch (const std::exception& error) {
            std::cerr << "license-audit: " << error.what() << "\n";
            return usage(argv[0]);
        }
    }

    if (inputs.manifestPath.empty() || inputs.spdxMapPath.empty()) {
        std::cerr << "license-audit: --manifest and --spdx-map are required\n";
        return usage(argv[0]);
    }

    try {
        const recto::tools::VcpkgManifest manifest = recto::tools::loadManifest(inputs);
        const recto::tools::QtModuleList qtModules = recto::tools::loadQtModules(inputs.qtModulesPath);

        recto::tools::Report report = recto::tools::audit(manifest, qtModules);

        // The GCC plugin condition is checked here as well as by TA-9 at configure time.
        // Deliberate duplication: the licence claim should not rest on a single place, and
        // this tool is what a release audit actually runs.
        if (!pluginList.empty()) {
            for (recto::tools::Finding& finding : recto::tools::checkCompilerPlugins(pluginList.string())) {
                report.findings.push_back(std::move(finding));
            }
        }

        // THIRD-PARTY.md is emitted even when there are findings, so the reader can see the
        // full inventory next to what failed rather than only an error.
        //
        // But it is a COMMITTED artifact, and a gate must never quietly degrade one. Only a
        // run holding the inputs that make the inventory complete may rewrite it: the Qt link
        // closure (--qt-modules) and an installed vcpkg tree (--vcpkg-installed), which
        // supplies exact versions from vcpkg own SBOM. The CI gates job and the `gates`
        // preset have neither by design, and a run there would replace catch2 version with
        // "(not resolved)" and drop every Qt row -- a smaller file that still reads as
        // plausible, which is the dangerous kind of wrong. Skipping is not a failure: the
        // audit itself ran, and its verdict is unaffected.
        if (!emitPath.empty()) {
            std::string missing;
            // The gates configuration writes an EMPTY qt-modules.txt, so the flag being
            // present proves nothing -- what matters is a real link closure behind it.
            if (qtModules.modules.empty()) missing = "no Qt link closure";
            if (inputs.vcpkgInstalledDir.empty()) {
                if (!missing.empty()) missing += ", ";
                missing += "no --vcpkg-installed";
            }

            if (missing.empty()) {
                // Binary mode: the renderer emits '\n', and this file is stored with LF
                // (.gitattributes). Letting the CRT translate would rewrite all of it as CRLF
                // on every Windows run and show up as a whole-file diff.
                std::ofstream out(emitPath, std::ios::binary);
                if (!out) {
                    std::cerr << "license-audit: cannot write " << emitPath.string() << "\n";
                    return 2;
                }
                out << recto::tools::renderThirdPartyMarkdown(report, manifest);
            } else {
                std::cout << "license-audit: " << emitPath.filename().string()
                          << " left unchanged -- incomplete inventory (" << missing << ")\n";
            }
        }

        // Warnings are printed in BOTH branches and always labelled. A warning about a
        // component this configuration does not build must never be mistaken for a licence
        // breach in something we ship -- and must never be silently dropped either, because
        // it is the early notice that a future stage has a problem waiting for it.
        auto printWarnings = [&report]() {
            for (const recto::tools::Finding& finding : report.findings) {
                if (finding.blocking) continue;
                std::cout << "warning   " << finding.subject << ": " << finding.detail
                          << "\n              [declared but not built by this configuration"
                             " -- becomes blocking when its feature is enabled]\n";
            }
        };

        if (report.ok()) {
            printWarnings();
            std::cout << "license-audit: clean -- " << report.entries.size()
                      << " components, " << qtModules.modules.size() << " Qt modules";
            if (!report.findings.empty()) {
                std::cout << ", " << report.findings.size() << " warning(s)";
            }
            std::cout << "\n";
            return 0;
        }

        for (const recto::tools::Finding& finding : report.findings) {
            if (!finding.blocking) continue;
            std::cout << "BLOCKING  " << finding.subject << ": " << finding.detail << "\n";
            if (!finding.why.empty()) std::cout << "    why: " << finding.why << "\n";
        }
        printWarnings();
        std::cout << "\nlicense-audit: " << report.blockingCount() << " blocking, "
                  << (report.findings.size() - report.blockingCount()) << " warning(s)\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "license-audit: " << error.what() << "\n";
        return 2;
    }
}
