// The translation catalogs and the locale declaration, read as data.
//
// ---------------------------------------------------------------------------------------------
// THE ONE DESIGN DECISION THIS FILE ENCODES: THE CATALOG KEY IS THE `<source>`, AND `en` IS A REAL
// CATALOG RATHER THAN THE IDENTITY.
//
// Qt's default convention makes the English sentence the key. RawPage cannot use it, and the reason
// is already written into `core/Result.h`: `core/` must hold no user-facing English, so an `Error`
// carries a catalog key (`error.pdf.badXref`) and `ActionId.def` carries one per action
// (`action.file.open`). Those keys have to resolve somewhere, so the keys ARE the source strings and
// every locale -- `en` included -- supplies the text.
//
// Three consequences worth stating, because each is a thing someone will otherwise "fix":
//
//   * A missing key in `en` is a real, detectable defect rather than an invisible fallback to the
//     key itself. With source-text-as-key it would render as `action.file.open` on a button and
//     nobody would notice until a screenshot.
//   * `--lang zh-TW` loading nothing is detectable for the same reason (guide case A-72).
//   * `lupdate` cannot seed these files from the sources, because the sources do not contain the
//     English. The catalogs are hand-maintained data, and `missing-key` plus `action-label` are
//     what keep them honest.

#ifndef RAWPAGE_I18N_LINT_CATALOG_H
#define RAWPAGE_I18N_LINT_CATALOG_H

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rawpage::tools {

struct CatalogEntry {
    std::string key;           // the <source> text, which is our catalog key
    std::string translation;
    std::string context;       // the <context><name>, by convention the key's first segment
    bool        unfinished = false;   // Linguist's type="unfinished"
    int         line = 0;
};

class Catalog {
public:
    // Throws std::runtime_error with a usable message if the file is missing or unreadable.
    static Catalog loadFromFile(const std::filesystem::path& path);

    const std::string& language() const { return language_; }
    const std::filesystem::path& path() const { return path_; }
    const std::vector<CatalogEntry>& entries() const { return entries_; }

    bool contains(const std::string& key) const;
    const CatalogEntry* find(const std::string& key) const;

    std::size_t unfinishedCount() const;

    // Keys declared more than once. A duplicate is not a merge conflict marker's worth of drama --
    // it is one of the two spellings silently winning, which is why it is reported rather than
    // resolved.
    std::vector<std::string> duplicateKeys() const;

private:
    std::filesystem::path     path_;
    std::string               language_;
    std::vector<CatalogEntry> entries_;
    std::map<std::string, std::size_t> index_;
};

struct LocaleDeclaration {
    std::string code;          // "en", "zh_TW"
    std::string catalog;       // repo-relative path to the .ts
    bool        shipped = false;
    std::string plannedFor;    // why it is not shipped yet, and when it will be -- D1-14
};

// i18n/locales.json. The `shipped` flag is what makes `missing-key` a rule that can actually fail:
// with every locale unshipped it would be vacuous, and with every locale shipped `zh_CN` and `ja`
// would fail from the day they were declared. D1-14 makes their absence DATA THE GATE READS rather
// than a case nobody wrote.
class LocaleSet {
public:
    static LocaleSet loadFromFile(const std::filesystem::path& path);

    const std::string& sourceLocale() const { return sourceLocale_; }
    const std::vector<LocaleDeclaration>& locales() const { return locales_; }
    const std::filesystem::path& path() const { return path_; }

    const LocaleDeclaration* find(const std::string& code) const;

private:
    std::filesystem::path          path_;
    std::string                    sourceLocale_;
    std::vector<LocaleDeclaration> locales_;
};

} // namespace rawpage::tools

#endif // RAWPAGE_I18N_LINT_CATALOG_H
