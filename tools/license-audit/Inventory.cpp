#include "tools/license-audit/Inventory.h"

#include "tools/common/Json.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace recto::tools {
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

} // namespace recto::tools
