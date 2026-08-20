// The layering rules, loaded from data rather than compiled in.
//
// tools/layer-lint/rules.json is the single place the dependency direction is written
// as something a machine can check. Keeping it out of the code is the point: widening
// what a layer may include should be a reviewable one-line diff with a reason attached,
// not a recompile that nobody reads.

#ifndef RAWPAGE_LAYER_LINT_RULESET_H
#define RAWPAGE_LAYER_LINT_RULESET_H

#include <filesystem>
#include <string>
#include <vector>

namespace rawpage::tools {

struct Layer {
    std::string name;
    std::vector<std::string> paths;          // repo-relative prefixes that belong to this layer
    std::vector<std::string> mayInclude;     // internal prefixes this layer may include
    std::vector<std::string> denyExternal;   // substrings of external includes that are forbidden here
    std::string note;                        // printed with a violation: WHY the rule exists
};

struct TokenRule {
    std::string kind;                        // stable id, e.g. "fpdf" or "filesystem-write"
    std::vector<std::string> patterns;       // plain substrings, matched on the code part of a line
    std::vector<std::string> allowedPaths;   // prefixes where the token is legitimate
    bool escapable = false;                  // may an inline // layer-lint: allow-<kind>(reason) waive it
    std::string message;
};

class RuleSet {
public:
    // Throws std::runtime_error with a usable message on a malformed or missing file.
    static RuleSet loadFromFile(const std::filesystem::path& path);

    // First match wins, so rules.json lists the more specific prefix first.
    // Returns nullptr for a path outside every declared layer.
    const Layer* layerFor(const std::string& repoRelativePath) const;

    bool isExcluded(const std::string& repoRelativePath) const;
    bool isScannableExtension(const std::filesystem::path& path) const;

    // True only for a kind that some rule actually declares AND marks escapable. A waiver
    // naming anything else waives nothing, so counting it as active would overstate the
    // census D0-4 relies on -- "allow-<kind>" in this file's own prose was one such (I-20).
    bool isEscapableKind(const std::string& kind) const;

    const std::vector<Layer>& layers() const { return layers_; }
    const std::vector<TokenRule>& tokenRules() const { return tokenRules_; }
    const std::vector<std::string>& excludedPaths() const { return excluded_; }
    const std::filesystem::path& sourcePath() const { return sourcePath_; }

private:
    std::vector<Layer> layers_;
    std::vector<TokenRule> tokenRules_;
    std::vector<std::string> excluded_;
    std::vector<std::string> extensions_;
    std::filesystem::path sourcePath_;
};

} // namespace rawpage::tools

#endif // RAWPAGE_LAYER_LINT_RULESET_H
