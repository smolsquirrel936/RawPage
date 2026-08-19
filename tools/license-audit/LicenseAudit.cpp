#include "tools/license-audit/LicenseAudit.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace recto::tools {
namespace {

// Normalize for comparison only: trim, collapse internal runs of whitespace, and fold
// case. SPDX identifiers are case-insensitive per the specification, and "GPL-3.0-or-later
//  WITH  GCC-exception-3.1" with a stray double space is the same licence as without it.
// Nothing here splits the expression -- the whole string is compared, which is the point.
std::string normalize(const std::string& expression) {
    std::string out;
    out.reserve(expression.size());
    bool pendingSpace = false;
    for (char c : expression) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isspace(uc)) {
            if (!out.empty()) pendingSpace = true;
            continue;
        }
        if (pendingSpace) { out.push_back(' '); pendingSpace = false; }
        out.push_back(static_cast<char>(std::tolower(uc)));
    }
    return out;
}

const std::vector<std::string> kSpdxAllowlist = {
    // Copied verbatim from Tech-Stack-Plan.md 8.2. This is the ONLY place the allowlist
    // appears in the tree; a second copy is how two copies drift apart.
    "BSD-2-Clause",
    "BSD-3-Clause",
    "MIT",
    "Apache-2.0",
    "Zlib",
    "BSL-1.0",
    "Unicode-DFS-2016",
    "FTL",
    "LGPL-3.0",
    "libpng-2.0",
    "libtiff",
    "IJG",
    "MS-RL",
    "ZPL-2.1",
    // The GCC runtime, and ONLY in this exact expression. Dropping the WITH clause --
    // by adding a non-free compiler plugin, or by pulling a differently licensed runtime
    // -- must fail the build rather than pass silently.
    "GPL-3.0-or-later WITH GCC-exception-3.1",
};

const std::vector<std::string> kQtModuleAllowlist = {
    // The nine modules of Tech-Stack-Plan.md 6.1.
    "Core", "Gui", "Widgets", "Network", "Svg",
    "PrintSupport", "Concurrent", "Multimedia", "LinguistTools",
};

// Modules whose only open-source licence is GPL-3.0. Named individually so the failure
// says WHY rather than "not on the allowlist" -- a generic message invites the reader to
// add the module to the allowlist, which is exactly the wrong repair.
bool isGplOnlyQtModule(const std::string& module, std::string& why) {
    if (module == "Charts" || module == "DataVisualization" || module == "VirtualKeyboard") {
        why = "Qt " + module + "'s only open-source licence is GPL-3.0. Linking it would "
              "relicense Recto's own proprietary source. This is decision D4; it is "
              "release-gate tested, and Qt Charts is installed in this machine's Qt prefix, "
              "so the trap is one find_package() line away. There is no allowlist entry to add.";
        return true;
    }
    return false;
}

} // namespace

const std::vector<std::string>& spdxAllowlist() { return kSpdxAllowlist; }
const std::vector<std::string>& qtModuleAllowlist() { return kQtModuleAllowlist; }

bool isAllowedSpdx(const std::string& expression) {
    const std::string candidate = normalize(expression);
    if (candidate.empty()) return false;
    for (const std::string& allowed : kSpdxAllowlist) {
        if (candidate == normalize(allowed)) return true;
    }
    return false;
}

Report audit(const VcpkgManifest& manifest, const QtModuleList& qtModules) {
    Report report;
    report.entries = manifest.dependencies;

    for (const Dependency& dep : manifest.dependencies) {
        const bool blocking = dep.active;
        if (dep.spdx.empty() || normalize(dep.spdx) == "noassertion") {
            report.findings.push_back(Finding{
                dep.name,
                "no SPDX licence expression could be determined",
                "Every shipped component needs a recorded licence. Add it to "
                "tools/license-audit/spdx-map.json with the upstream URL, or install the "
                "port so its SBOM can be read.", blocking});
            continue;
        }

        if (!isAllowedSpdx(dep.spdx)) {
            report.findings.push_back(Finding{
                dep.name,
                "licence '" + dep.spdx + "' is not on the allowlist",
                "Tech-Stack-Plan.md 8.2 lists every permitted expression. Matching is on the "
                "whole expression, so a GPL-family licence cannot slip through by resembling "
                "an allowed one -- and 'GPL-3.0-or-later' without 'WITH GCC-exception-3.1' is "
                "a build failure by design.", blocking});
        }

        if (dep.noticeRequired && !dep.hasNotice) {
            if (dep.noticeChecked) {
                report.findings.push_back(Finding{
                    dep.name,
                    "Apache-2.0 component with no NOTICE file recorded",
                    "Apache-2.0 requires shipping any NOTICE file verbatim.", blocking});
            } else {
                report.findings.push_back(Finding{
                    dep.name,
                    "Apache-2.0 component whose NOTICE file could not be checked "
                    "(no installed tree supplied)",
                    "Re-run with --vcpkg-installed, or rely on the release gate, which builds "
                    "every feature. Reported rather than assumed either way.", false});
            }
        }

        // Any LGPL branch, not just the exact identifier Qt happens to use: the
        // corresponding-source obligation attaches to the licence family, and Hunspell is
        // now a second LGPL component. Matching only "LGPL-3.0" would have let an
        // "LGPL-2.1-or-later" component ship with no source offer.
        if (normalize(dep.spdx).rfind("lgpl", 0) == 0 && dep.sourceOffer.empty()) {
            report.findings.push_back(Finding{
                dep.name,
                "LGPL-3.0 component with no source-offer URL",
                "LGPL-3.0 obliges us to offer the corresponding source of the exact version "
                "shipped, including any patch we apply (Tech-Stack-Plan.md 8.1).", blocking});
        }
    }

    for (const std::string& module : qtModules.modules) {
        std::string why;
        if (isGplOnlyQtModule(module, why)) {
            report.findings.push_back(Finding{"Qt6::" + module, "GPL-3.0-only Qt module is linked", why});
            continue;
        }
        const bool allowed = std::find(kQtModuleAllowlist.begin(), kQtModuleAllowlist.end(), module)
                             != kQtModuleAllowlist.end();
        if (!allowed) {
            report.findings.push_back(Finding{
                "Qt6::" + module,
                "Qt module is not among the nine this project takes",
                "Adding a Qt module is a licensing decision. Record it in Tech-Stack-Plan.md "
                "6.1 first, then extend the allowlist."});
        }
    }

    return report;
}

std::vector<Finding> checkCompilerPlugins(const std::string& pluginListPath) {
    std::vector<Finding> findings;
    std::ifstream in(pluginListPath);
    if (!in) return findings;          // absent is not a failure: a portable build has none

    std::string line;
    while (std::getline(in, line)) {
        const bool blank = line.find_first_not_of(" \t\r\n") == std::string::npos;
        if (blank) continue;
        findings.push_back(Finding{
            "compiler",
            "a GCC plugin is in play: " + line,
            "A non-free compiler plugin voids the GCC Runtime Library Exception, which is the "
            "only thing permitting Recto to ship proprietary binaries linked against libstdc++. "
            "Losing it would relicense Recto's own source under GPL-3.0 (Tech-Stack-Plan.md 8.1)."});
    }
    return findings;
}

std::string renderThirdPartyMarkdown(const Report& report, const VcpkgManifest& manifest) {
    std::ostringstream out;
    out << "# Third-Party Notices\n\n"
        << "**Generated by `tools/license-audit`. Do not edit by hand.**\n\n"
        << "Regenerate with `cmake --build --preset x64-release --target checks`. This file is "
           "committed and tagged per release (Tech-Stack-Plan.md 8.2), and the About -> "
           "Third-Party Notices dialog is built from the same data, so a dependency cannot ship "
           "without appearing in both.\n\n"
        << "Project: " << manifest.projectName << " " << manifest.projectVersion << "\n\n";

    out << "## Components\n\n"
        << "| Component | Version | SPDX | Origin | Group | Attribution owed | Upstream |\n"
        << "|---|---|---|---|---|---|---|\n";

    for (const Dependency& dep : report.entries) {
        out << "| " << dep.name
            << " | " << (dep.version.empty() ? "*(not resolved)*" : dep.version)
            << " | `" << dep.spdx << "`"
            << " | " << dep.origin
            << " | " << (dep.featureGroup.empty() ? "always" : dep.featureGroup)
            // Four states, not two. "**MISSING**" used to cover both "required, looked,
            // absent" and "required, never looked because this configuration does not build
            // it" -- the first is a release blocker and the second is routine, and an auditor
            // reading the table could not tell them apart (I-17).
            // Attribution is owed by EVERY licence here, so this cell never reads "n/a" -- it
            // says what is owed. The old column tracked Apache-2.0 4(d) alone and printed "n/a"
            // for eleven BSD/MIT/zlib/BSL rows, which reads as "nothing owed" and is the
            // opposite of Tech-Stack-Plan 8.1 (I-22).
            << " | " << (!dep.noticeRequired  ? "licence text"
                         : !dep.noticeChecked ? "licence text, NOTICE not checked"
                         : dep.hasNotice      ? "licence text and NOTICE"
                                              : "licence text and NOTICE MISSING")
            << " | " << (dep.upstream.empty() ? "" : dep.upstream)
            << " |\n";
    }

    // The two odd-looking cells are the first thing an outside reader stops on, and neither
    // was explained anywhere on the page (I-17, found by M-06).
    out << "\n> **Reading this table.** *(not resolved)* under Version means the component is "
           "**declared** in `vcpkg.json` but its feature is not enabled in this configuration, "
           "so there is no installed tree to read a version from. It is not shipped in this "
           "build; a version appears once the stage that needs it turns the feature on.\n"
           "\n> **Attribution owed** is never empty: every licence here requires that the "
           "component copyright notice and licence text be retained and shipped "
           "(Tech-Stack-Plan.md 8.1), so the cell says what is owed rather than whether "
           "anything is. `licence text` is that baseline. **Apache-2.0 alone** adds 4(d), "
           "propagating an upstream `NOTICE` file verbatim, which is why only those rows "
           "mention NOTICE: `and NOTICE` means one was found, `NOTICE not checked` means the "
           "component is not built in this configuration so nothing was looked for, and "
           "`NOTICE MISSING` means the installed tree was there and the file was not -- a "
           "release blocker. The licence files live in `res/licenses/`, are reproduced in "
           "About -> Third-Party Notices and in the installed `licenses` folder, and are "
           "verified by the release gate (8.3). `res/licenses/` does not exist yet; "
           "populating it is Stage 15 work.\n";

    out << "\n## Licence elections\n\n"
        << "Where a component is offered under more than one licence, the one we take is "
           "recorded here explicitly rather than inferred.\n\n";
    bool anyElection = false;
    for (const Dependency& dep : report.entries) {
        if (dep.election.empty()) continue;
        out << "- **" << dep.name << "** — " << dep.election << "\n";
        anyElection = true;
    }
    if (!anyElection) out << "*(none among the currently resolved components)*\n";

    out << "\n## Source offers\n\n";
    bool anyOffer = false;
    for (const Dependency& dep : report.entries) {
        if (dep.sourceOffer.empty()) continue;
        out << "- **" << dep.name << "** (`" << dep.spdx << "`) — " << dep.sourceOffer << "\n";
        anyOffer = true;
    }
    if (!anyOffer) out << "*(none required among the currently resolved components)*\n";

    out << "\n## Notes\n\n"
        << "- Qt is linked **dynamically** and shipped as replaceable DLLs beside the executable. "
           "It is never statically linked, never signature-locked, and never anti-tampered: "
           "user-replaceability is an LGPL-3.0 obligation and is release-gate tested.\n"
        << "- The GCC runtime (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`) is "
           "`GPL-3.0-or-later WITH GCC-exception-3.1`. The exception is what permits shipping "
           "proprietary binaries, and it holds only while the build uses stock GCC with no "
           "non-free plugin.\n"
        << "- Recto's own source is proprietary. No file in `core/`, `pdf/`, `ui/`, `app/`, or "
           "`platform/` may be copied from a copyleft project.\n";

    return out.str();
}

} // namespace recto::tools
