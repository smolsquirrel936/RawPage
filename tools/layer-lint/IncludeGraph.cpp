#include "tools/layer-lint/IncludeGraph.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace rawpage::tools {
namespace {

std::string toRepoRelative(const fs::path& file, const fs::path& root) {
    std::string s = fs::relative(file, root).generic_string();
    return s;
}

// Splits a line into the part the compiler sees and the trailing // comment. Tokens are
// matched only in the first, escapes only in the second -- otherwise every mention of
// std::ofstream in a comment (including the ones in this project's own documentation of
// the rule) would be reported as a violation.
//
// Block comments are NOT tracked. A /* */ spanning lines can therefore hide a token from
// the escape logic or expose a commented-out one. Accepted deliberately: tracking block
// comments correctly means tracking string literals and raw strings too, which is a
// tokenizer, and a tokenizer is a compiler's job. The failure mode is a false positive,
// which is visible and fixable, not a false negative.
struct SplitLine {
    std::string code;
    std::string comment;
};

SplitLine splitLine(const std::string& line) {
    // Skip over double-quoted strings so a "//" inside one is not treated as a comment.
    bool inString = false;
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\') { ++i; continue; }
        if (c == '"') { inString = !inString; continue; }
        if (!inString && c == '/' && line[i + 1] == '/') {
            return {line.substr(0, i), line.substr(i + 2)};
        }
    }
    return {line, {}};
}

std::string trim(const std::string& s) {
    const size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool parseInclude(const std::string& code, Include& out) {
    const std::string text = trim(code);
    if (text.rfind("#", 0) != 0) return false;

    size_t pos = 1;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    if (text.compare(pos, 7, "include") != 0) return false;
    pos += 7;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    if (pos >= text.size()) return false;

    char closing = '\0';
    if (text[pos] == '"') { closing = '"'; out.angled = false; }
    else if (text[pos] == '<') { closing = '>'; out.angled = true; }
    else return false;                       // computed include -- nothing to check

    const size_t start = pos + 1;
    const size_t end = text.find(closing, start);
    if (end == std::string::npos) return false;

    out.target = text.substr(start, end - start);
    return !out.target.empty();
}

// A waiver looks like this, in the trailing comment of the line it waives:
//     std::ofstream out(path);   // layer-lint: allow-filesystem-write(reason)
//
// Two structural guards, because without them this tool counted its OWN documentation as
// three active waivers and the census read 3 where the truth was 0 (I-20). D0-4's value
// rests on that census: it is how anyone notices waivers creeping from a handful to a
// hundred, and a number that includes prose cannot do that job.
//
//   codeOnLine  - a waiver waives a violation ON ITS LINE. With no code there is nothing to
//                 waive, which is what "// // layer-lint: allow-..." in a comment block is.
//   inner //    - a second // before the marker means the marker is being QUOTED, as in
//                 "... from // layer-lint: allow-filesystem-write(reason)". Applied waivers
//                 sit in the first comment on the line.
bool parseEscape(const std::string& comment, bool codeOnLine, Escape& out) {
    if (!codeOnLine) return false;

    const std::string text = trim(comment);
    const std::string marker = "layer-lint:";
    const size_t markerPos = text.find(marker);
    if (markerPos == std::string::npos) return false;
    if (text.find("//") < markerPos) return false;

    size_t pos = markerPos + marker.size();
    while (pos < text.size() && text[pos] == ' ') ++pos;

    const std::string allow = "allow-";
    if (text.compare(pos, allow.size(), allow) != 0) return false;
    pos += allow.size();

    const size_t open = text.find('(', pos);
    if (open == std::string::npos) return false;
    const size_t close = text.find(')', open);
    if (close == std::string::npos) return false;

    out.kind = text.substr(pos, open - pos);
    out.reason = trim(text.substr(open + 1, close - open - 1));
    return !out.kind.empty();
}

std::string resolveInclude(const Include& include, const std::string& includingPath, const fs::path& root) {
    // Repo-relative form. Checked for ANGLED includes too, which this function used to
    // dismiss as "always external by convention" -- but every layer target does
    // target_include_directories(... PUBLIC "${CMAKE_SOURCE_DIR}"), so
    //     #include <pdf/cos/CosDict.h>
    // compiles exactly like the quoted spelling and reached core/ unreported (I-15). A real
    // system header does not exist under the repo root, so this test separates them.
    if (fs::exists(root / include.target)) {
        return fs::path(include.target).lexically_normal().generic_string();
    }

    if (include.angled) return {};            // a genuine external or system header

    // Then the sibling form, which is legitimate within one directory.
    const fs::path parent = fs::path(includingPath).parent_path();
    const fs::path sibling = parent / include.target;
    if (fs::exists(root / sibling)) {
        // lexically_normal() collapses the ../ segments, so that
        //     core/doc/../../pdf/cos/CosDict.h
        // becomes pdf/cos/CosDict.h. Without it the string still STARTS with "core/", and
        // the layer lookup matches on path prefix -- so an outward include was filed as an
        // internal one and passed clean (I-15). This is the spelling an IDE inserts.
        return sibling.lexically_normal().generic_string();
    }

    return {};                                 // third-party or system header
}

} // namespace

IncludeGraph IncludeGraph::scan(const fs::path& root, const RuleSet& rules) {
    if (!fs::is_directory(root)) {
        throw std::runtime_error("not a directory: " + root.string());
    }

    IncludeGraph graph;
    graph.root_ = root;

    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (it->is_directory()) {
            const std::string dir = toRepoRelative(it->path(), root) + "/";
            if (rules.isExcluded(dir)) { it.disable_recursion_pending(); }
            continue;
        }
        if (!it->is_regular_file()) continue;
        if (!rules.isScannableExtension(it->path())) continue;

        const std::string relative = toRepoRelative(it->path(), root);
        if (rules.isExcluded(relative)) continue;

        SourceFile file;
        file.path = relative;
        file.layer = rules.layerFor(relative);

        std::ifstream in(it->path());
        if (!in) throw std::runtime_error("cannot read: " + it->path().string());

        std::string line;
        int lineNumber = 0;
        while (std::getline(in, line)) {
            ++lineNumber;
            const SplitLine parts = splitLine(line);

            Include include;
            include.line = lineNumber;
            if (parseInclude(parts.code, include)) {
                include.resolved = resolveInclude(include, relative, root);
                file.includes.push_back(std::move(include));
            }

            for (const TokenRule& rule : rules.tokenRules()) {
                for (const std::string& pattern : rule.patterns) {
                    if (parts.code.find(pattern) != std::string::npos) {
                        file.tokens.push_back(TokenHit{rule.kind, pattern, lineNumber});
                        break;                 // one hit per rule per line is enough to report
                    }
                }
            }

            if (!parts.comment.empty()) {
                Escape escape;
                escape.line = lineNumber;
                if (parseEscape(parts.comment, !trim(parts.code).empty(), escape)
                    && rules.isEscapableKind(escape.kind)) {
                    file.escapes.push_back(std::move(escape));
                }
            }
        }

        graph.files_.push_back(std::move(file));
    }

    std::sort(graph.files_.begin(), graph.files_.end(),
              [](const SourceFile& a, const SourceFile& b) { return a.path < b.path; });
    return graph;
}

size_t IncludeGraph::includeCount() const {
    size_t total = 0;
    for (const SourceFile& file : files_) total += file.includes.size();
    return total;
}

} // namespace rawpage::tools
