// i18n-lint's configuration, loaded from data rather than compiled in.
//
// `tools/i18n-lint/rules.json` holds the sink list, the wrapper list, the scanned paths and the
// exclusions. Keeping them out of the code is the point: adding a sink as `ui/` grows should be a
// one-line diff with a reason in the commit message, not a recompile of a gate.

#ifndef RAWPAGE_I18N_LINT_I18NRULES_H
#define RAWPAGE_I18N_LINT_I18NRULES_H

#include <filesystem>
#include <string>
#include <vector>

namespace rawpage::tools {

class I18nRules {
public:
    static I18nRules loadFromFile(const std::filesystem::path& path);

    // Calls that put text in front of a person. A literal reaching one of these is the defect.
    const std::vector<std::string>& sinks() const { return sinks_; }

    // Calls that make a literal legitimate: tr(), QT_TR_NOOP(), QCoreApplication::translate().
    const std::vector<std::string>& wrappers() const { return wrappers_; }

    // Repo-relative prefixes that are scanned for the literal rule -- ui/, app/, core/.
    const std::vector<std::string>& scannedPaths() const { return scanned_; }

    const std::vector<std::string>& excludedPaths() const { return excluded_; }

    bool isScanned(const std::string& repoRelativePath) const;
    bool isExcluded(const std::string& repoRelativePath) const;
    bool isScannableExtension(const std::filesystem::path& path) const;

    // The catalog namespaces the `unknown-key` rule is anchored on -- "action", "shell", "error"
    // and the rest. See rules.json: the scanner's idea of a key is deliberately generous because
    // orphan-key only reports, and a BLOCKING rule over the same set would fail the build on
    // settings.ini and pdfium.dll.
    const std::vector<std::string>& keyFamilies() const { return keyFamilies_; }

    // Whether a reference is one this project is responsible for translating: its first
    // dot-separated segment is a declared family, and it is not a namespace prefix.
    bool isCatalogKey(const std::string& reference) const;

    // The one escapable rule (Stage1-AppShell-Plan.md 5.6). `unwrapped-literal` is a heuristic, so
    // it gets the same inline waiver shape layer-lint already uses -- and the same --explain census,
    // because an escape hatch nobody can see is an escape hatch that becomes the norm.
    bool isEscapableKind(const std::string& kind) const;

    const std::filesystem::path& sourcePath() const { return sourcePath_; }

private:
    std::vector<std::string> sinks_;
    std::vector<std::string> wrappers_;
    std::vector<std::string> scanned_;
    std::vector<std::string> excluded_;
    std::vector<std::string> extensions_;
    std::vector<std::string> escapableKinds_;
    std::vector<std::string> keyFamilies_;
    std::filesystem::path    sourcePath_;
};

} // namespace rawpage::tools

#endif // RAWPAGE_I18N_LINT_I18NRULES_H
