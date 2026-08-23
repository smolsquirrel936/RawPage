#include "tools/i18n-lint/Catalog.h"

#include "tools/common/Json.h"

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace rawpage::tools {
namespace {

std::string readFile(const fs::path& path, const char* what)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(std::string("cannot open ") + what + ": " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// A deliberately small XML reader, for files this repository writes.
//
// It handles exactly the Linguist shape we author: <TS language="..">, <context><name>..</name>,
// and <message><source>..</source><translation [type="unfinished"]>..</translation>. It does NOT
// handle CDATA, namespaces, or attributes in an order it does not expect -- and that is the right
// trade for a gate that must link nothing but the standard library (D0-1). A real XML parser is a
// dependency, and a dependency in a gate is a gate that cannot run before the build.
//
// The failure mode is loud: a shape it cannot read yields no entries, and an empty source catalog
// fails `action-label` on the first action rather than passing quietly.
std::string unescapeXml(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') { out.push_back(text[i]); continue; }
        const std::size_t semi = text.find(';', i);
        if (semi == std::string::npos) { out.push_back(text[i]); continue; }
        const std::string entity = text.substr(i, semi - i + 1);
        if (entity == "&amp;") out.push_back('&');
        else if (entity == "&lt;") out.push_back('<');
        else if (entity == "&gt;") out.push_back('>');
        else if (entity == "&quot;") out.push_back('"');
        else if (entity == "&apos;") out.push_back('\'');
        else { out += entity; }
        i = semi;
    }
    return out;
}

int lineOf(const std::string& text, std::size_t offset)
{
    int line = 1;
    for (std::size_t i = 0; i < offset && i < text.size(); ++i) {
        if (text[i] == '\n') ++line;
    }
    return line;
}

// Reads attribute `name` from the tag starting at `tagStart`.
std::string attribute(const std::string& text, std::size_t tagStart, const std::string& name)
{
    const std::size_t tagEnd = text.find('>', tagStart);
    if (tagEnd == std::string::npos) return {};
    const std::string tag = text.substr(tagStart, tagEnd - tagStart);
    const std::size_t at = tag.find(name + "=\"");
    if (at == std::string::npos) return {};
    const std::size_t valueStart = at + name.size() + 2;
    const std::size_t valueEnd = tag.find('"', valueStart);
    if (valueEnd == std::string::npos) return {};
    return tag.substr(valueStart, valueEnd - valueStart);
}

// Text between <tag ...> and </tag>, starting the search at `from`. Returns false if absent.
bool elementText(const std::string& text, const std::string& tag, std::size_t from,
                 std::size_t limit, std::string& out, std::size_t& tagStart, std::size_t& afterEnd)
{
    const std::string open = "<" + tag;
    const std::string close = "</" + tag + ">";

    const std::size_t start = text.find(open, from);
    if (start == std::string::npos || start >= limit) return false;
    const std::size_t contentStart = text.find('>', start);
    if (contentStart == std::string::npos) return false;
    const std::size_t end = text.find(close, contentStart);
    if (end == std::string::npos || end >= limit) return false;

    out = unescapeXml(text.substr(contentStart + 1, end - contentStart - 1));
    tagStart = start;
    afterEnd = end + close.size();
    return true;
}

} // namespace

Catalog Catalog::loadFromFile(const fs::path& path)
{
    Catalog catalog;
    catalog.path_ = path;

    const std::string text = readFile(path, "catalog");

    const std::size_t tsTag = text.find("<TS");
    if (tsTag != std::string::npos) catalog.language_ = attribute(text, tsTag, "language");

    // One pass over <context> blocks, so an entry carries the context it was declared in.
    std::size_t contextPos = 0;
    while (true) {
        const std::size_t contextStart = text.find("<context", contextPos);
        if (contextStart == std::string::npos) break;
        const std::size_t contextEnd = text.find("</context>", contextStart);
        const std::size_t limit = contextEnd == std::string::npos ? text.size() : contextEnd;

        std::string contextName;
        std::size_t nameStart = 0;
        std::size_t afterName = contextStart;
        elementText(text, "name", contextStart, limit, contextName, nameStart, afterName);

        std::size_t messagePos = afterName;
        while (true) {
            const std::size_t messageStart = text.find("<message", messagePos);
            if (messageStart == std::string::npos || messageStart >= limit) break;
            const std::size_t messageEnd = text.find("</message>", messageStart);
            const std::size_t messageLimit = messageEnd == std::string::npos ? limit : messageEnd;

            CatalogEntry entry;
            entry.context = contextName;

            std::size_t sourceStart = 0;
            std::size_t afterSource = messageStart;
            if (elementText(text, "source", messageStart, messageLimit, entry.key, sourceStart,
                            afterSource)) {
                entry.line = lineOf(text, sourceStart);

                std::size_t translationStart = 0;
                std::size_t afterTranslation = afterSource;
                if (elementText(text, "translation", afterSource, messageLimit, entry.translation,
                                translationStart, afterTranslation)) {
                    entry.unfinished =
                        attribute(text, translationStart, "type") == "unfinished";
                } else {
                    // No <translation> element at all is the same defect as an unfinished one, and
                    // is reported the same way rather than as a missing key: the key IS there.
                    entry.unfinished = true;
                }

                if (catalog.index_.find(entry.key) == catalog.index_.end()) {
                    catalog.index_.emplace(entry.key, catalog.entries_.size());
                }
                catalog.entries_.push_back(std::move(entry));
            }

            messagePos = messageEnd == std::string::npos ? messageLimit : messageEnd + 10;
        }

        contextPos = contextEnd == std::string::npos ? text.size() : contextEnd + 10;
    }

    return catalog;
}

bool Catalog::contains(const std::string& key) const
{
    return index_.find(key) != index_.end();
}

const CatalogEntry* Catalog::find(const std::string& key) const
{
    const auto it = index_.find(key);
    return it == index_.end() ? nullptr : &entries_[it->second];
}

std::size_t Catalog::unfinishedCount() const
{
    std::size_t count = 0;
    for (const CatalogEntry& entry : entries_) {
        if (entry.unfinished) ++count;
    }
    return count;
}

std::vector<std::string> Catalog::duplicateKeys() const
{
    std::set<std::string> seen;
    std::set<std::string> duplicates;
    for (const CatalogEntry& entry : entries_) {
        if (!seen.insert(entry.key).second) duplicates.insert(entry.key);
    }
    return std::vector<std::string>(duplicates.begin(), duplicates.end());
}

LocaleSet LocaleSet::loadFromFile(const fs::path& path)
{
    LocaleSet set;
    set.path_ = path;

    const JsonValue root = parseJson(readFile(path, "locales file"));
    if (!root.isObject()) {
        throw std::runtime_error("locales file is not a JSON object: " + path.string());
    }

    set.sourceLocale_ = root["sourceLocale"].asString();
    if (set.sourceLocale_.empty()) set.sourceLocale_ = "en";

    for (const JsonValue& value : root["locales"].asArray()) {
        LocaleDeclaration locale;
        locale.code = value["code"].asString();
        locale.catalog = value["catalog"].asString();
        locale.shipped = value["shipped"].asBool(false);
        locale.plannedFor = value["plannedFor"].asString();
        if (locale.code.empty()) {
            throw std::runtime_error("a locale in " + path.string() + " has no code");
        }
        if (locale.catalog.empty()) {
            throw std::runtime_error("locale '" + locale.code + "' in " + path.string()
                                     + " names no catalog");
        }
        set.locales_.push_back(std::move(locale));
    }

    if (set.locales_.empty()) {
        throw std::runtime_error("no locales declared in " + path.string());
    }
    if (set.find(set.sourceLocale_) == nullptr) {
        throw std::runtime_error("the source locale '" + set.sourceLocale_ + "' is not declared in "
                                 + path.string());
    }

    return set;
}

const LocaleDeclaration* LocaleSet::find(const std::string& code) const
{
    for (const LocaleDeclaration& locale : locales_) {
        if (locale.code == code) return &locale;
    }
    return nullptr;
}

} // namespace rawpage::tools
