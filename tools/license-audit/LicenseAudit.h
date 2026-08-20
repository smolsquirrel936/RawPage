// license-audit -- the gate that mechanically enforces "no GPL or AGPL in the tree".
//
// THE ONE RULE THAT MATTERS MOST HERE: matching is on the WHOLE, NORMALIZED SPDX
// EXPRESSION -- never a substring. Tech-Stack-Plan.md 8.2 is emphatic about it because a
// naive `contains("GPL")` test is wrong in both directions at once:
//
//     "LGPL-3.0"                                  -> would be REJECTED (false positive: Qt)
//     "GPL-3.0-or-later WITH GCC-exception-3.1"   -> would be REJECTED (false positive:
//                                                    the GCC runtime in every binary we ship)
//     ...and once someone "fixes" those by loosening the test, bare "GPL-3.0-or-later"
//     starts passing, which is the actual hazard.
//
// So the allowlist below holds complete expressions, and an entry passes only by being
// equal to one of them.

#ifndef RAWPAGE_LICENSE_AUDIT_LICENSEAUDIT_H
#define RAWPAGE_LICENSE_AUDIT_LICENSEAUDIT_H

#include <string>
#include <vector>

namespace rawpage::tools {

// One shipped component: a vcpkg port, a Qt module, or something declared by hand in
// spdx-map.json (the GCC runtime, the mingw-w64 runtime, PDFium, WiX).
struct Dependency {
    std::string name;
    std::string version;         // empty when no installed tree was supplied
    std::string spdx;            // the whole expression, as declared
    std::string upstream;
    std::string sourceOffer;     // required where the license demands one (Qt/LGPL)
    std::string election;        // e.g. "FTL elected over GPL-2.0" for a dual-licensed dep
    std::string origin;          // "vcpkg", "qt", "toolchain", "prebuilt"
    std::string featureGroup;    // which vcpkg feature pulls it in, "" for always
    bool hasNotice = false;      // Apache-2.0 requires shipping NOTICE verbatim
    bool noticeRequired = false;

    // Whether the NOTICE question could be ANSWERED at all. It can only be checked against
    // an installed tree, and the CI gates job has none. A check that could not run must not
    // report a failure -- the same principle that keeps exit 2 distinct from exit 1.
    bool noticeChecked = false;

    // Is this component part of THIS build? A dependency behind a vcpkg feature that is
    // switched off is declared but not shipped.
    //
    // This distinction decides whether a licence problem BLOCKS or WARNS, and it matters:
    // vcpkg.json declares every dependency the product will ever need so that risk A5 can
    // be answered early (decision D0-3). Without the distinction, Stage 0 could not go
    // green until every future dependency's licence was reconciled -- and the tempting way
    // to make that go away is to widen the allowlist, which is the precise failure this
    // gate exists to prevent. So: warn now, block when the feature is switched on.
    bool active = true;
};

struct VcpkgManifest {
    std::string projectName;
    std::string projectVersion;
    std::vector<Dependency> dependencies;
};

struct QtModuleList {
    std::vector<std::string> modules;   // as recorded by rawpage_link_qt() into qt-modules.txt
};

struct Finding {
    std::string subject;
    std::string detail;
    std::string why;
    // false when the component is declared but not shipped by this build: reported so it
    // cannot be forgotten, but it does not fail the gate until its feature is enabled.
    bool blocking = true;
};

struct Report {
    std::vector<Dependency> entries;
    std::vector<Finding> findings;
    bool ok() const {
        for (const Finding& f : findings) { if (f.blocking) return false; }
        return true;
    }
    size_t blockingCount() const {
        size_t n = 0;
        for (const Finding& f : findings) { if (f.blocking) ++n; }
        return n;
    }
};

// The entry point named in the Stage 0 plan. Pure: no I/O, so it can be exercised
// directly against constructed inputs.
Report audit(const VcpkgManifest& manifest, const QtModuleList& qtModules);

// Renders THIRD-PARTY.md. Generated, never hand-edited -- Tech-Stack-Plan.md 8.2 requires
// it committed and tagged per release, and the About -> Third-Party Notices dialog is
// built from the same data so a new dependency cannot ship without appearing in both.
std::string renderThirdPartyMarkdown(const Report& report, const VcpkgManifest& manifest);

// Belt-and-braces re-check of the condition the GCC Runtime Library Exception rests on.
// cmake/ToolchainAssert.cmake (TA-9) already asserts this at configure time and writes
// the file; checking it again here means the licence claim does not depend on one place.
std::vector<Finding> checkCompilerPlugins(const std::string& pluginListPath);

// Exposed for the gate fixtures: whole-expression allowlist membership.
bool isAllowedSpdx(const std::string& expression);
const std::vector<std::string>& spdxAllowlist();
const std::vector<std::string>& qtModuleAllowlist();

} // namespace rawpage::tools

#endif // RAWPAGE_LICENSE_AUDIT_LICENSEAUDIT_H
