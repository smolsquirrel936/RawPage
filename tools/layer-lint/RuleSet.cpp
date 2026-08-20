#include "tools/layer-lint/RuleSet.h"

#include "tools/common/Json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rawpage::tools {
namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open rules file: " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Case-INSENSITIVE, because resolution and classification must not disagree. Windows
// filesystems are case-insensitive, so
//     #include "Pdf/Cos/CosDict.h"
// resolves happily and comes back spelled as written; a case-sensitive prefix test then
// failed to match "pdf/" and filed an outward include as an external header (I-15). The
// tree's own paths are lowercase by convention, so folding case here cannot merge two
// layers that were meant to be distinct.
bool startsWith(const std::string& text, const std::string& prefix) {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(text[i]);
        const unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

} // namespace

RuleSet RuleSet::loadFromFile(const std::filesystem::path& path) {
    RuleSet rules;
    rules.sourcePath_ = path;

    const JsonValue root = parseJson(readFile(path));
    if (!root.isObject()) throw std::runtime_error("rules file is not a JSON object: " + path.string());

    rules.extensions_ = root["extensions"].asStringList();
    if (rules.extensions_.empty()) rules.extensions_ = {".h", ".hpp", ".cpp", ".cc"};

    rules.excluded_ = root["exclude"].asStringList();

    for (const auto& [name, value] : root["layers"].asObject()) {
        Layer layer;
        layer.name = name;
        layer.paths = value["paths"].asStringList();
        layer.mayInclude = value["mayInclude"].asStringList();
        layer.denyExternal = value["denyExternal"].asStringList();
        layer.note = value["note"].asString();
        if (layer.paths.empty()) {
            throw std::runtime_error("layer '" + name + "' declares no paths in " + path.string());
        }
        rules.layers_.push_back(std::move(layer));
    }
    if (rules.layers_.empty()) throw std::runtime_error("no layers declared in " + path.string());

    // Longest prefix first, so "core/settings/" is considered before "core/" no matter
    // how the JSON object happened to be ordered. std::map sorts keys alphabetically,
    // which would otherwise decide a semantic question by accident.
    for (Layer& layer : rules.layers_) {
        std::sort(layer.paths.begin(), layer.paths.end(),
                  [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    }
    std::stable_sort(rules.layers_.begin(), rules.layers_.end(), [](const Layer& a, const Layer& b) {
        const size_t aLen = a.paths.empty() ? 0 : a.paths.front().size();
        const size_t bLen = b.paths.empty() ? 0 : b.paths.front().size();
        return aLen > bLen;
    });

    for (const JsonValue& value : root["tokenRules"].asArray()) {
        TokenRule rule;
        rule.kind = value["kind"].asString();
        rule.patterns = value["patterns"].asStringList();
        rule.allowedPaths = value["allowedPaths"].asStringList();
        rule.escapable = value["escapable"].asBool(false);
        rule.message = value["message"].asString();
        if (rule.kind.empty() || rule.patterns.empty()) {
            throw std::runtime_error("a tokenRule in " + path.string() + " lacks a kind or patterns");
        }
        rules.tokenRules_.push_back(std::move(rule));
    }

    return rules;
}

const Layer* RuleSet::layerFor(const std::string& repoRelativePath) const {
    for (const Layer& layer : layers_) {
        for (const std::string& prefix : layer.paths) {
            if (startsWith(repoRelativePath, prefix)) return &layer;
        }
    }
    return nullptr;
}

bool RuleSet::isEscapableKind(const std::string& kind) const {
    for (const TokenRule& rule : tokenRules_) {
        if (rule.kind == kind) return rule.escapable;
    }
    return false;
}

bool RuleSet::isExcluded(const std::string& repoRelativePath) const {
    for (const std::string& prefix : excluded_) {
        if (startsWith(repoRelativePath, prefix)) return true;
    }
    return false;
}

bool RuleSet::isScannableExtension(const std::filesystem::path& path) const {
    const std::string ext = path.extension().string();
    for (const std::string& candidate : extensions_) {
        if (ext == candidate) return true;
    }
    return false;
}

} // namespace rawpage::tools
