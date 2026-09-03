#include "tools/license-audit/Inventory.h"

#include "tools/common/Json.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace rawpage::tools {
namespace {

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open: " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string trim(const std::string& s) {
    const size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// A dependency entry in vcpkg.json is either "name" or { "name": "...", ... }.
std::string dependencyName(const JsonValue& value) {
    if (value.isString()) return value.asString();
    if (value.isObject()) return value["name"].asString();
    return {};
}

// Reads share/<pkg>/vcpkg.spdx.json. licenseConcluded is what vcpkg determined; the
// declared field is frequently NOASSERTION, so concluded is preferred.
struct SbomFacts {
    std::string version;
    std::string spdx;
    std::string downloadLocation;
};

bool readSbom(const fs::path& installedDir, const std::string& port, SbomFacts& out) {
    const fs::path sbom = installedDir / "share" / port / "vcpkg.spdx.json";
    if (!fs::exists(sbom)) return false;
    try {
        const JsonValue root = parseJson(readFile(sbom));
        const JsonArray& packages = root["packages"].asArray();
        if (packages.empty()) return false;
        const JsonValue& pkg = packages.front();
        out.version = pkg["versionInfo"].asString();
        out.downloadLocation = pkg["downloadLocation"].asString();

        const std::string concluded = pkg["licenseConcluded"].asString();
        const std::string declared = pkg["licenseDeclared"].asString();
        out.spdx = (!concluded.empty() && concluded != "NOASSERTION") ? concluded : declared;
        return true;
    } catch (const std::exception&) {
        return false;                 // a malformed SBOM falls back to the map, not a crash
    }
}

bool hasNoticeFile(const fs::path& installedDir, const std::string& port) {
    for (const char* name : {"NOTICE", "NOTICE.txt", "NOTICE.md"}) {
        if (fs::exists(installedDir / "share" / port / name)) return true;
    }
    return false;
}

} // namespace

VcpkgManifest loadManifest(const InventoryInputs& inputs) {
    VcpkgManifest manifest;

    const JsonValue root = parseJson(readFile(inputs.manifestPath));
    manifest.projectName = root["name"].asString();
    manifest.projectVersion = root["version"].asString();

    // The map supplies everything vcpkg cannot: licences for components outside vcpkg,
    // URLs, source offers, and dual-licence elections.
    const JsonValue map = parseJson(readFile(inputs.spdxMapPath));

    struct MapEntry {
        std::string spdx, upstream, sourceOffer, election, origin;
        bool noticeRequired = false;
    };
    std::map<std::string, MapEntry> overrides;
    for (const auto& [name, value] : map["components"].asObject()) {
        // Keys beginning with '$' are comments. vcpkg's own manifest format uses that
        // convention, and JSON has no comment syntax, so the map uses it too. Without this
        // skip, a note explaining a licence becomes a component with no licence -- which is
        // exactly what the first run of this tool reported.
        if (!name.empty() && name.front() == '$') continue;

        MapEntry entry;
        entry.spdx = value["spdx"].asString();
        entry.upstream = value["upstream"].asString();
        entry.sourceOffer = value["sourceOffer"].asString();
        entry.election = value["election"].asString();
        entry.origin = value["origin"].asString();
        entry.noticeRequired = value["noticeRequired"].asBool(false);
        overrides[name] = std::move(entry);
    }

    // Which features count as part of this build. Explicit --feature flags win; otherwise
    // it is default-features from vcpkg.json, which is literally what vcpkg installs.
    std::vector<std::string> activeFeatures = inputs.activeFeatures;
    if (activeFeatures.empty()) {
        activeFeatures = root["default-features"].asStringList();
    }
    auto isActiveFeature = [&](const std::string& feature) {
        if (feature.empty()) return true;      // a top-level dependency is always installed
        return std::find(activeFeatures.begin(), activeFeatures.end(), feature) != activeFeatures.end();
    };

    auto addPort = [&](const std::string& port, const std::string& featureGroup) {
        if (port.empty()) return;
        Dependency dep;
        dep.name = port;
        dep.origin = "vcpkg";
        dep.featureGroup = featureGroup;
        dep.active = isActiveFeature(featureGroup);

        SbomFacts facts;
        if (!inputs.vcpkgInstalledDir.empty() && readSbom(inputs.vcpkgInstalledDir, port, facts)) {
            dep.version = facts.version;
            dep.spdx = facts.spdx;
            dep.upstream = facts.downloadLocation;
            dep.hasNotice = hasNoticeFile(inputs.vcpkgInstalledDir, port);
            dep.noticeChecked = true;
        }

        auto it = overrides.find(port);
        if (it != overrides.end()) {
            // The map wins on licence, because a dual-licensed component's ELECTION is our
            // decision, not vcpkg's observation -- FreeType is FTL here, not GPL-2.0.
            if (!it->second.spdx.empty()) dep.spdx = it->second.spdx;
            if (!it->second.upstream.empty()) dep.upstream = it->second.upstream;
            dep.sourceOffer = it->second.sourceOffer;
            dep.election = it->second.election;
            dep.noticeRequired = it->second.noticeRequired;
        }
        manifest.dependencies.push_back(std::move(dep));
    };

    for (const JsonValue& value : root["dependencies"].asArray()) {
        addPort(dependencyName(value), {});
    }
    for (const auto& [featureName, feature] : root["features"].asObject()) {
        for (const JsonValue& value : feature["dependencies"].asArray()) {
            addPort(dependencyName(value), featureName);
        }
    }

    // --- THE RESOLVED CLOSURE (issue I-02) ----------------------------------------------
    // Everything above builds the list from what we WROTE DOWN. That is not what we ship.
    //
    // Measured 2026-08-31 by P-3's dry-run and confirmed 2026-09-03 by an actual install: the
    // `codecs` feature pulls in `liblzma` (through tiff) and the `text` feature pulls in
    // `brotli`, `bzip2` and `libiconv` -- and `vcpkg.json` names none of the four, so `addPort`
    // was never called for them and the gate had no verdict on any of them. `libiconv` is the one
    // that matters: GNU libiconv is LGPL-2.1-or-later and carries a source-offer obligation.
    //
    // Nothing found was GPL, so there was no breach -- but CLAUDE.md and Tech-Stack-Plan.md 8.2
    // both describe this tool as what mechanically enforces the no-GPL rule, and a gate that
    // audits the manifest rather than the closure cannot make that claim. Stage 10 (OpenSSL) and
    // Stage 16 (tesseract, leptonica) add much deeper closures on top of it.
    //
    // An installed tree IS the closure: vcpkg writes share/<port>/vcpkg.spdx.json for every
    // package it actually installed, transitive ones included. So the fix is to enumerate that
    // directory rather than to maintain a second list of what depends on what -- a hand-kept list
    // would be one more thing to drift, which is the failure being repaired here.
    //
    // WITH NO INSTALLED TREE this loop does nothing, and that is correct rather than a gap: the
    // CI `gates` job has no vcpkg (D0-1) and still has to fail on a bad licence, which it does
    // from the map. The map therefore carries a row for each KNOWN transitive port, so the
    // no-GPL rule is enforced in both configurations.
    if (!inputs.vcpkgInstalledDir.empty()) {
        std::vector<std::string> alreadyKnown;
        alreadyKnown.reserve(manifest.dependencies.size());
        for (const Dependency& dep : manifest.dependencies) alreadyKnown.push_back(dep.name);

        const fs::path shareDir = inputs.vcpkgInstalledDir / "share";
        std::error_code ignored;
        if (fs::is_directory(shareDir, ignored)) {
            // Sorted, so the report and THIRD-PARTY.md do not reorder themselves between runs on
            // filesystems that enumerate differently.
            std::vector<std::string> found;
            for (const auto& entry : fs::directory_iterator(shareDir, ignored)) {
                if (!entry.is_directory()) continue;
                const std::string port = entry.path().filename().string();
                // Only directories carrying an SBOM are packages. vcpkg also drops alias and
                // documentation directories in here (`jpeg`, `png`, `man`, `doc`), and counting
                // those as components would put four licence-less rows in the report.
                if (!fs::exists(entry.path() / "vcpkg.spdx.json")) continue;
                if (std::find(alreadyKnown.begin(), alreadyKnown.end(), port) != alreadyKnown.end())
                    continue;
                found.push_back(port);
            }
            std::sort(found.begin(), found.end());

            for (const std::string& port : found) {
                Dependency dep;
                dep.name = port;
                dep.origin = "vcpkg";
                // Named rather than left blank: a reader of the report has to be able to tell a
                // port we chose from one that arrived with something else, because the two have
                // different answers to "can we drop it".
                dep.featureGroup = "(transitive)";
                dep.active = true;      // it is in the installed tree, so it is in this build

                SbomFacts facts;
                if (readSbom(inputs.vcpkgInstalledDir, port, facts)) {
                    dep.version = facts.version;
                    dep.spdx = facts.spdx;
                    dep.upstream = facts.downloadLocation;
                    dep.hasNotice = hasNoticeFile(inputs.vcpkgInstalledDir, port);
                    dep.noticeChecked = true;
                }
                auto it = overrides.find(port);
                if (it != overrides.end()) {
                    // The map still wins on licence. vcpkg concludes `LicenseRef-vcpkg-null` for
                    // liblzma -- it could not determine one -- and an unverified marker must never
                    // become an allowlist hit.
                    if (!it->second.spdx.empty()) dep.spdx = it->second.spdx;
                    if (!it->second.upstream.empty()) dep.upstream = it->second.upstream;
                    dep.sourceOffer = it->second.sourceOffer;
                    dep.election = it->second.election;
                    dep.noticeRequired = it->second.noticeRequired;
                }
                manifest.dependencies.push_back(std::move(dep));
            }
        }
    }

    // Components that never appear in vcpkg.json because they do not come from vcpkg:
    // Qt, the GCC and mingw-w64 runtimes, the prebuilt PDFium DLL, WiX. They are shipped,
    // so they are audited -- the GCC runtime especially, since it is the one GPL-family
    // licence in every binary we produce.
    for (const auto& [name, entry] : overrides) {
        if (entry.origin == "vcpkg") continue;
        Dependency dep;
        dep.name = name;
        dep.spdx = entry.spdx;
        dep.upstream = entry.upstream;
        dep.sourceOffer = entry.sourceOffer;
        dep.election = entry.election;
        dep.origin = entry.origin;
        dep.noticeRequired = entry.noticeRequired;
        dep.hasNotice = entry.noticeRequired;   // shipped from res/licenses/, not from vcpkg
        dep.noticeChecked = true;
        dep.version = map["components"].asObject().at(name)["version"].asString();
        manifest.dependencies.push_back(std::move(dep));
    }

    return manifest;
}

QtModuleList loadQtModules(const fs::path& qtModulesPath) {
    QtModuleList list;
    if (qtModulesPath.empty() || !fs::exists(qtModulesPath)) return list;

    std::ifstream in(qtModulesPath);
    std::string line;
    while (std::getline(in, line)) {
        const std::string module = trim(line);
        if (!module.empty()) list.modules.push_back(module);
    }
    return list;
}

} // namespace rawpage::tools
