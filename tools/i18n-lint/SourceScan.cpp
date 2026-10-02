#include "tools/i18n-lint/SourceScan.h"

#include "tools/common/LineScan.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace rawpage::tools {
namespace {

// A literal, wherever it sits in a line, in every spelling that produces one:
//     "text"   QStringLiteral("text")   u"text"   u8"text"   QLatin1String("text")
// The prefix does not matter -- what matters is the quoted run, so this finds them all with one
// scan rather than one pattern per spelling. I-15's finding, applied before rather than after: a
// rule written against one spelling catches one spelling.
struct QuotedRun {
    std::size_t begin = 0;      // index of the opening quote
    std::size_t end = 0;        // index just past the closing quote
    std::string text;           // between the quotes, escapes left as written
};

std::vector<QuotedRun> quotedRuns(const std::string& code)
{
    std::vector<QuotedRun> runs;
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (code[i] == '\'') {
            // A character literal. Skipped so that '"' does not open a phantom string.
            std::size_t j = i + 1;
            while (j < code.size() && code[j] != '\'') {
                if (code[j] == '\\') ++j;
                ++j;
            }
            i = j;
            continue;
        }
        if (code[i] != '"') continue;

        QuotedRun run;
        run.begin = i;
        std::size_t j = i + 1;
        std::string text;
        while (j < code.size() && code[j] != '"') {
            if (code[j] == '\\' && j + 1 < code.size()) {
                text.push_back(code[j]);
                text.push_back(code[j + 1]);
                j += 2;
                continue;
            }
            text.push_back(code[j]);
            ++j;
        }
        if (j >= code.size()) break;          // an unterminated literal: a compile error, not ours
        run.end = j + 1;
        run.text = std::move(text);
        runs.push_back(std::move(run));
        i = j;
    }
    return runs;
}

// A literal only matters to a reader if it has letters in it. `", "`, `"%1"`, `"/"` and `""` are
// punctuation and formatting, and reporting them would bury the rule in waivers within a day --
// which is precisely how a heuristic gate gets switched off.
bool hasLetter(const std::string& text)
{
    for (const char c : text) {
        if (std::isalpha(static_cast<unsigned char>(c))) return true;
    }
    return false;
}

// The span of a call's arguments: from the '(' at `openParen` to its matching ')', or to the end of
// the line. Line-oriented, so a call broken across lines is measured only as far as its first line
// -- reported here rather than silently, because it is the known limit of scanning before a build.
std::size_t matchingParen(const std::string& code, std::size_t openParen)
{
    int depth = 0;
    bool inString = false;
    for (std::size_t i = openParen; i < code.size(); ++i) {
        const char c = code[i];
        if (inString) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '(') ++depth;
        else if (c == ')') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return code.size();
}

// True when `pos` sits inside a wrapper call -- tr(...), QT_TR_NOOP(...), translate(...) -- within
// `code`. Computed by spans rather than by "does the line contain tr(", because
//     setText(tr("Save") + ": " + name)
// contains a wrapper AND an unwrapped literal, and only the second is the defect.
bool insideWrapper(const std::string& code, std::size_t pos, const std::vector<std::string>& wrappers)
{
    for (const std::string& wrapper : wrappers) {
        std::size_t at = 0;
        while ((at = code.find(wrapper, at)) != std::string::npos) {
            const std::size_t open = at + wrapper.size() - 1;   // the wrapper pattern ends in '('
            if (open < code.size() && code[open] == '(') {
                const std::size_t close = matchingParen(code, open);
                if (pos > open && pos < close) return true;
            }
            at += wrapper.size();
        }
    }
    return false;
}

// A catalog-key-shaped literal: dotted, no spaces, lowercase-ish first segment. This is how the
// orphan-key rule learns what the source references, and it is deliberately generous -- an
// over-broad reference set makes orphan-key quieter, never louder, and orphan-key is the one rule
// that reports rather than fails.
bool looksLikeCatalogKey(const std::string& text)
{
    if (text.find('.') == std::string::npos) return false;
    if (text.find(' ') != std::string::npos) return false;
    if (text.empty() || !std::islower(static_cast<unsigned char>(text.front()))) return false;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '.' && c != '_') return false;
    }
    return true;
}

std::string toRepoRelative(const fs::path& file, const fs::path& root)
{
    return fs::relative(file, root).generic_string();
}

} // namespace

SourceScanResult scanSources(const fs::path& root, const I18nRules& rules)
{
    if (!fs::is_directory(root)) {
        throw std::runtime_error("not a directory: " + root.string());
    }

    SourceScanResult result;

    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (it->is_directory()) {
            const std::string dir = toRepoRelative(it->path(), root) + "/";
            if (rules.isExcluded(dir)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file()) continue;
        if (!rules.isScannableExtension(it->path())) continue;

        const std::string relative = toRepoRelative(it->path(), root);
        if (rules.isExcluded(relative)) continue;
        if (!rules.isScanned(relative)) continue;

        std::ifstream in(it->path());
        if (!in) throw std::runtime_error("cannot read: " + it->path().string());

        ++result.filesScanned;

        std::string line;
        int lineNumber = 0;
        while (std::getline(in, line)) {
            ++lineNumber;
            const SplitLine parts = splitLine(line);
            const std::vector<QuotedRun> runs = quotedRuns(parts.code);

            // Waivers first, so a waived line still counts its waiver in the census.
            Waiver waiver;
            bool waived = false;
            if (!parts.comment.empty()
                && parseWaiver(parts.comment, !trim(parts.code).empty(), "i18n-lint:", waiver)
                && rules.isEscapableKind(waiver.kind)) {
                result.waivers.push_back(WaiverHit{relative, lineNumber, waiver.kind, waiver.reason, false});
                waived = true;
            }

            // AN ACTION'S STRING ID IS NOT A CATALOG KEY (S5 guide I-08). It is a permanent public name, never
            // looked up in a catalog -- and `compare.documents` is an action while `compare.` has also been a
            // catalog family since Stage 5 T13 (D5-20), so reading the id as a reference reported the action
            // table and the ribbon as `unknown-key`s. Two spellings name one: a row of the action table, which
            // names ONE key, its label (the last quoted run, the column `actionLabelKeys` reads), and a JSON
            // value of an "action" member, which is how `res/ribbon.json` names a command.
            const bool actionRow = trim(parts.code).rfind("RAWPAGE_ACTION(", 0) == 0;
            const auto isActionValue = [&](std::size_t i) {
                if (i == 0 || runs[i - 1].text != "action") return false;
                return trim(parts.code.substr(runs[i - 1].end, runs[i].begin - runs[i - 1].end)) == ":";
            };
            for (std::size_t i = 0; i < runs.size(); ++i) {
                if (actionRow && i + 1 != runs.size()) continue;
                if (isActionValue(i)) continue;
                const QuotedRun& run = runs[i];
                if (looksLikeCatalogKey(run.text)) {
                    result.keyReferences.push_back(KeyReference{relative, lineNumber, run.text});
                }
            }

            // The sink rule.
            for (const std::string& sink : rules.sinks()) {
                std::size_t at = 0;
                while ((at = parts.code.find(sink, at)) != std::string::npos) {
                    const std::size_t open = at + sink.size() - 1;   // sinks are written ending in '('
                    at += sink.size();
                    if (open >= parts.code.size() || parts.code[open] != '(') continue;

                    const std::size_t close = matchingParen(parts.code, open);

                    for (const QuotedRun& run : runs) {
                        if (run.begin <= open || run.begin >= close) continue;
                        ++result.literalsConsidered;
                        if (!hasLetter(run.text)) continue;
                        if (looksLikeCatalogKey(run.text)) continue;   // a key, not a sentence
                        if (insideWrapper(parts.code, run.begin, rules.wrappers())) continue;

                        if (waived) {
                            result.waivers.back().applied = true;
                            continue;
                        }
                        result.literals.push_back(LiteralHit{relative, lineNumber, sink, run.text});
                    }
                }
            }
        }
    }

    std::sort(result.literals.begin(), result.literals.end(),
              [](const LiteralHit& a, const LiteralHit& b) {
                  return a.file == b.file ? a.line < b.line : a.file < b.file;
              });
    return result;
}

std::vector<std::string> actionLabelKeys(const fs::path& actionDefPath)
{
    std::ifstream in(actionDefPath);
    if (!in) throw std::runtime_error("cannot open action table: " + actionDefPath.string());

    std::vector<std::string> keys;
    std::string line;
    while (std::getline(in, line)) {
        const std::string code = trim(splitLine(line).code);
        // The macro DEFINITION lines start with `#define`, so requiring the row to start with the
        // macro name is what keeps the table's own header out of the census.
        if (code.rfind("RAWPAGE_ACTION(", 0) != 0) continue;

        // The label key is the last quoted run on the row. Taking it positionally rather than by
        // counting commas means a future column added before it does not silently shift what this
        // reads -- and the .def's rule 3 is that the column exists precisely so this can read it.
        std::size_t lastOpen = std::string::npos;
        std::size_t lastClose = std::string::npos;
        for (std::size_t i = 0; i < code.size(); ++i) {
            if (code[i] != '"') continue;
            const std::size_t closing = code.find('"', i + 1);
            if (closing == std::string::npos) break;
            lastOpen = i;
            lastClose = closing;
            i = closing;
        }
        if (lastOpen == std::string::npos) continue;
        keys.push_back(code.substr(lastOpen + 1, lastClose - lastOpen - 1));
    }
    return keys;
}

} // namespace rawpage::tools
