// A line-oriented scan of every source file: its includes, its flagged tokens, and its
// inline escapes.
//
// WHY A TEXT SCAN AND NOT A REAL PREPROCESSOR: this must run BEFORE anything is
// compiled, with no compile database and no Qt (decision D0-1), because a layering
// violation should fail CI in seconds rather than after a full build. The cost is that
// conditional compilation is invisible to it -- an #include inside an #ifdef is reported
// whether or not that branch is taken. For a rule that says "this layer may never name
// that one", counting a disabled include as a violation is the right side to err on.

#ifndef RAWPAGE_LAYER_LINT_INCLUDEGRAPH_H
#define RAWPAGE_LAYER_LINT_INCLUDEGRAPH_H

#include "tools/layer-lint/RuleSet.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rawpage::tools {

struct Include {
    std::string target;        // exactly as written between the quotes or angle brackets
    std::string resolved;      // repo-relative path if it resolves inside the tree, else empty
    int line = 0;
    bool angled = false;       // <foo> rather than "foo"
};

struct TokenHit {
    std::string kind;          // matches TokenRule::kind
    std::string pattern;       // the substring that matched
    int line = 0;
};

struct Escape {
    std::string kind;          // e.g. "filesystem-write" from // layer-lint: allow-filesystem-write(reason)
    std::string reason;
    int line = 0;
};

struct SourceFile {
    std::string path;          // repo-relative, forward slashes
    const Layer* layer = nullptr;
    std::vector<Include> includes;
    std::vector<TokenHit> tokens;
    std::vector<Escape> escapes;
};

class IncludeGraph {
public:
    // Walks `root` recursively. Throws std::runtime_error if root is not a directory.
    static IncludeGraph scan(const std::filesystem::path& root, const RuleSet& rules);

    const std::vector<SourceFile>& files() const { return files_; }
    const std::filesystem::path& root() const { return root_; }
    size_t includeCount() const;

private:
    std::filesystem::path root_;
    std::vector<SourceFile> files_;
};

} // namespace rawpage::tools

#endif // RAWPAGE_LAYER_LINT_INCLUDEGRAPH_H
