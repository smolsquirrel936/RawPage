#include "tools/i18n-lint/I18nRules.h"

#include "tools/common/Json.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rawpage::tools {
namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open rules file: " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Case-insensitive, for the same measured reason layer-lint's is (I-15): Windows resolves
// "Ui/Shell/MainWindow.cpp" happily and hands the path back spelled as written, and a
// case-sensitive prefix test then decides the file is in no scanned layer and skips it.
bool startsWith(const std::string& text, const std::string& prefix)
{
    if (text.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(text[i]);
        const unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

} // namespace

I18nRules I18nRules::loadFromFile(const std::filesystem::path& path)
{
    I18nRules rules;
    rules.sourcePath_ = path;

    const JsonValue root = parseJson(readFile(path));
    if (!root.isObject()) {
        throw std::runtime_error("rules file is not a JSON object: " + path.string());
    }

    rules.extensions_ = root["extensions"].asStringList();
    if (rules.extensions_.empty()) rules.extensions_ = {".h", ".hpp", ".cpp", ".cc"};

    rules.sinks_ = root["sinks"].asStringList();
    rules.wrappers_ = root["wrappers"].asStringList();
    rules.scanned_ = root["scannedPaths"].asStringList();
    rules.excluded_ = root["exclude"].asStringList();
    rules.escapableKinds_ = root["escapableKinds"].asStringList();
    rules.keyFamilies_ = root["keyFamilies"].asStringList();

    // Each of these is load-bearing, and an empty one turns a rule into a rule that cannot fire.
    // A gate that silently checks nothing is worse than no gate, because it is reported as green.
    if (rules.sinks_.empty()) {
        throw std::runtime_error("no sinks declared in " + path.string()
                                 + " -- the unwrapped-literal rule could never fire");
    }
    if (rules.wrappers_.empty()) {
        throw std::runtime_error("no wrappers declared in " + path.string()
                                 + " -- every wrapped literal would be reported");
    }
    if (rules.scanned_.empty()) {
        throw std::runtime_error("no scannedPaths declared in " + path.string()
                                 + " -- no source file would be examined");
    }
    if (rules.keyFamilies_.empty()) {
        // The same argument as the three above, and it is the newest of them: unknown-key is
        // anchored entirely on this list, so an empty one is a rule that reports nothing while the
        // gate still prints "clean" (issue I-04).
        throw std::runtime_error("no keyFamilies declared in " + path.string()
                                 + " -- the unknown-key rule could never fire");
    }

    return rules;
}

bool I18nRules::isScanned(const std::string& repoRelativePath) const
{
    for (const std::string& prefix : scanned_) {
        if (startsWith(repoRelativePath, prefix)) return true;
    }
    return false;
}

bool I18nRules::isExcluded(const std::string& repoRelativePath) const
{
    for (const std::string& prefix : excluded_) {
        if (startsWith(repoRelativePath, prefix)) return true;
    }
    return false;
}

bool I18nRules::isScannableExtension(const std::filesystem::path& path) const
{
    const std::string ext = path.extension().string();
    for (const std::string& candidate : extensions_) {
        if (ext == candidate) return true;
    }
    return false;
}

bool I18nRules::isCatalogKey(const std::string& reference) const
{
    // A trailing dot is a NAMESPACE reference covering every key beneath it -- several families are
    // built from a table at runtime, so the whole key is a literal nowhere (T14, issue I-27). It is
    // not a key and must not be looked up as one.
    if (reference.empty() || reference.back() == '.') return false;

    const std::size_t dot = reference.find('.');
    if (dot == std::string::npos) return false;

    const std::string family = reference.substr(0, dot);
    for (const std::string& candidate : keyFamilies_) {
        if (candidate == family) return true;
    }
    return false;
}

bool I18nRules::isEscapableKind(const std::string& kind) const
{
    for (const std::string& candidate : escapableKinds_) {
        if (candidate == kind) return true;
    }
    return false;
}

} // namespace rawpage::tools
