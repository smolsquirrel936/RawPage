// Assembling the component list from whatever evidence is available.
//
// Three sources, in decreasing order of authority:
//   1. vcpkg's own SPDX SBOM (share/<pkg>/vcpkg.spdx.json in an installed tree) -- exact
//      version and concluded licence, written by the tool that built the package.
//   2. tools/license-audit/spdx-map.json -- everything vcpkg cannot tell us: Qt, the GCC
//      and mingw-w64 runtimes, the prebuilt PDFium DLL, WiX; plus URLs, source offers, and
//      dual-licence elections.
//   3. vcpkg.json -- which ports are declared at all, and under which feature.
//
// The audit must run with NO installed tree, because the CI `gates` job has no vcpkg and
// no Qt (decision D0-1) and still has to fail on a bad licence. Without an install, the
// version column reads "(not resolved)" and the licence still comes from the map, so the
// no-GPL rule is enforced either way. Only the release gate (Stage 15) demands exact
// versions for every row.

#ifndef RAWPAGE_LICENSE_AUDIT_INVENTORY_H
#define RAWPAGE_LICENSE_AUDIT_INVENTORY_H

#include "tools/license-audit/LicenseAudit.h"

#include <filesystem>
#include <string>
#include <vector>

namespace rawpage::tools {

struct InventoryInputs {
    std::filesystem::path manifestPath;        // vcpkg.json                       (required)
    std::filesystem::path spdxMapPath;         // spdx-map.json                    (required)
    std::filesystem::path vcpkgInstalledDir;   // build/<preset>/vcpkg_installed/<triplet> (optional)
    std::filesystem::path qtModulesPath;       // qt-modules.txt                   (optional)

    // Features considered part of THIS build. Empty means "whatever vcpkg.json lists in
    // default-features", which is what the build actually installs.
    std::vector<std::string> activeFeatures;
};

VcpkgManifest loadManifest(const InventoryInputs& inputs);
QtModuleList loadQtModules(const std::filesystem::path& qtModulesPath);

} // namespace rawpage::tools

#endif // RAWPAGE_LICENSE_AUDIT_INVENTORY_H
