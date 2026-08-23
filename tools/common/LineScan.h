// Line-oriented source scanning, shared by the gates.
//
// WHY THIS IS SHARED AND NOT COPIED. Splitting a line into "the part the compiler sees" and "the
// trailing comment", and deciding whether a waiver marker is APPLIED or merely QUOTED, are both
// subtle enough that Stage 0 got them wrong in production (I-15, I-20) -- layer-lint counted its own
// documentation as three active waivers, and matched only the one spelling of a violation its
// fixtures happened to use. A second gate re-deriving that logic would re-derive the bugs, and the
// two copies would then drift apart in whichever direction each gate's fixtures happened to point.
//
// It stays header-only and standard-library-only (D0-1), because the gates link nothing else and
// that is what lets `checks` run on the bare Linux runner before anything is compiled.

#ifndef RAWPAGE_TOOLS_LINESCAN_H
#define RAWPAGE_TOOLS_LINESCAN_H

#include <string>

namespace rawpage::tools {

struct SplitLine {
    std::string code;      // what the compiler sees
    std::string comment;   // the trailing // comment, without the slashes
};

// Splits at the first `//` that is not inside a double-quoted string.
//
// Block comments are NOT tracked. A /* */ spanning lines can therefore hide a token from the waiver
// logic or expose a commented-out one. Accepted deliberately: tracking block comments correctly
// means tracking string literals and raw strings too, which is a tokenizer, and a tokenizer is a
// compiler's job. The failure mode is a false positive -- visible and fixable -- not a false
// negative.
inline SplitLine splitLine(const std::string& line)
{
    bool inString = false;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\') { ++i; continue; }
        if (c == '"') { inString = !inString; continue; }
        if (!inString && c == '/' && line[i + 1] == '/') {
            return {line.substr(0, i), line.substr(i + 2)};
        }
    }
    return {line, {}};
}

inline std::string trim(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

struct Waiver {
    std::string kind;     // e.g. "filesystem-write", "literal"
    std::string reason;
};

// A waiver looks like this, in the trailing comment of the line it waives:
//
//     std::ofstream out(path);   // layer-lint: allow-filesystem-write(reason)
//
// TWO STRUCTURAL GUARDS, and neither is theoretical -- without them layer-lint counted its own
// prose as three active waivers and reported a census of 3 where the truth was 0 (I-20). The value
// of a census is entirely that it can be trusted to show waivers creeping from a handful to a
// hundred, and a number that includes documentation cannot do that job.
//
//   codeOnLine  a waiver waives a violation ON ITS LINE. With no code there is nothing to waive,
//               which is exactly what a marker inside a comment block is.
//   inner //    a second `//` before the marker means the marker is being QUOTED, as in
//               "... written as // layer-lint: allow-filesystem-write(reason)". An APPLIED waiver
//               sits in the first comment on its line.
//
// `markerPrefix` is the owning gate's name plus a colon, so one gate's waivers can never silence
// another's rule.
inline bool parseWaiver(const std::string& comment, bool codeOnLine,
                        const std::string& markerPrefix, Waiver& out)
{
    if (!codeOnLine) return false;

    const std::string text = trim(comment);
    const std::size_t markerPos = text.find(markerPrefix);
    if (markerPos == std::string::npos) return false;
    if (text.find("//") < markerPos) return false;

    std::size_t pos = markerPos + markerPrefix.size();
    while (pos < text.size() && text[pos] == ' ') ++pos;

    const std::string allow = "allow-";
    if (text.compare(pos, allow.size(), allow) != 0) return false;
    pos += allow.size();

    const std::size_t open = text.find('(', pos);
    if (open == std::string::npos) return false;
    const std::size_t close = text.find(')', open);
    if (close == std::string::npos) return false;

    out.kind = text.substr(pos, open - pos);
    out.reason = trim(text.substr(open + 1, close - open - 1));
    return !out.kind.empty();
}

} // namespace rawpage::tools

#endif // RAWPAGE_TOOLS_LINESCAN_H
