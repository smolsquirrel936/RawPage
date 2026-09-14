// tools/mutation-lint -- implementation. Stage 3 T6. See the header for the rules and the blind spots.

#include "tools/mutation-lint/MutationLint.h"

#include "tools/common/LineScan.h"

#include <algorithm>
#include <fstream>
#include <cctype>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace rawpage::tools {
namespace {

namespace fs = std::filesystem;

const char* const kTablePath = "core/cmd/Commands.def";
const char* const kCommandsDir = "core/cmd/commands";
const char* const kTestsDir = "tests";
const char* const kExcludedTestsDir = "tests/gates";

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.generic_string());
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// The file with every `//` comment removed and every newline kept, so an offset still maps to its
// line. Block comments are not tracked -- LineScan.h says why, and the failure is a false positive.
std::string withoutLineComments(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        out += splitLine(line).code;
        out += '\n';
    }
    return out;
}

int lineAt(const std::string& text, std::size_t offset)
{
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
}

std::string relativeTo(const fs::path& root, const fs::path& path)
{
    return fs::relative(path, root).generic_string();
}

bool isSourceFile(const fs::path& path)
{
    const std::string ext = path.extension().string();
    return ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cc" || ext == ".cxx";
}

// Files in a directory tree, sorted, so the output order does not depend on the filesystem.
std::vector<fs::path> sourceFilesUnder(const fs::path& directory, const fs::path& excluded)
{
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(directory); it != fs::recursive_directory_iterator(); ++it) {
        if (!excluded.empty() && it->is_directory() && fs::equivalent(it->path(), excluded)) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file() && isSourceFile(it->path())) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

// ---- A hand-written scanner, NOT <regex> --------------------------------------------------------
// The first version used std::regex, and the Linux `portable-asan` job failed to BUILD it: GCC 13 at
// -O2 under -fsanitize=address raises -Wmaybe-uninitialized inside libstdc++'s own regex executor
// (std::function in _State), and -Werror made the library's false positive fatal (CI run 34873994971).
// No other file in the tree includes <regex>; the shapes below are small enough to read by hand, and
// a gate whose build depends on one compiler's opinion of a standard header is a gate CI can lose.

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::size_t skipSpace(const std::string& text, std::size_t pos)
{
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    return pos;
}

// The identifier starting at `pos`, or empty. Does not check the character before `pos`.
std::string identifierAt(const std::string& text, std::size_t pos)
{
    if (pos >= text.size() || !isIdentStart(text[pos])) return {};
    std::size_t end = pos + 1;
    while (end < text.size() && isIdentChar(text[end])) ++end;
    return text.substr(pos, end - pos);
}

// Calls `visit(name, offset)` for every identifier in `text` outside a double-quoted string literal.
template <typename Visit>
void forEachIdentifier(const std::string& text, Visit visit)
{
    std::size_t pos = 0;
    while (pos < text.size()) {
        const char c = text[pos];
        if (c == '"') {
            for (++pos; pos < text.size() && text[pos] != '"'; ++pos)
                if (text[pos] == '\\') ++pos;
            ++pos;
        } else if (isIdentStart(c)) {
            const std::string name = identifierAt(text, pos);
            visit(name, pos);
            pos += name.size();
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            while (pos < text.size() && isIdentChar(text[pos])) ++pos;     // 0x1F is not an identifier
        } else {
            ++pos;
        }
    }
}

// One entry of a base-specifier list names `Command`: optional access and `virtual` keywords, then a
// qualified name whose LAST component is exactly `Command` -- `CommandHistory` is not.
bool baseIsCommand(const std::string& base)
{
    std::istringstream words(base);
    std::string word;
    std::string qualified;
    bool keywords = true;
    while (words >> word) {
        if (keywords && (word == "public" || word == "protected" || word == "private" || word == "virtual"))
            continue;
        keywords = false;
        qualified += word;
    }
    const std::string name = "Command";
    if (qualified.size() < name.size() || qualified.compare(qualified.size() - name.size(), name.size(), name) != 0)
        return false;
    const std::string prefix = qualified.substr(0, qualified.size() - name.size());
    if (!prefix.empty() && (prefix.size() < 2 || prefix.compare(prefix.size() - 2, 2, "::") != 0)) return false;
    return std::all_of(prefix.begin(), prefix.end(), [](char c) { return isIdentChar(c) || c == ':'; });
}

void scanCommandClasses(MutationInputs& inputs)
{
    for (const fs::path& file : sourceFilesUnder(inputs.root / kCommandsDir, {})) {
        ++inputs.commandFilesScanned;
        const std::string text = withoutLineComments(readFile(file));
        forEachIdentifier(text, [&](const std::string& keyword, std::size_t at) {
            if (keyword != "class" && keyword != "struct") return;
            if (at > 0 && isIdentChar(text[at - 1])) return;
            // `class Name [final] :` -- a single colon, not the `::` of a qualified name.
            std::size_t pos = skipSpace(text, at + keyword.size());
            const std::string name = identifierAt(text, pos);
            if (name.empty()) return;
            pos = skipSpace(text, pos + name.size());
            if (identifierAt(text, pos) == "final") pos = skipSpace(text, pos + 5);
            if (pos >= text.size() || text[pos] != ':' || (pos + 1 < text.size() && text[pos + 1] == ':')) return;

            const std::size_t basesEnd = text.find_first_of("{;", pos + 1);
            if (basesEnd == std::string::npos) return;
            const std::string bases = text.substr(pos + 1, basesEnd - pos - 1);
            bool derives = false;
            std::size_t from = 0;
            while (from <= bases.size() && !derives) {
                const std::size_t comma = bases.find(',', from);
                derives = baseIsCommand(bases.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
                if (comma == std::string::npos) break;
                from = comma + 1;
            }
            if (derives) inputs.classes.push_back(CommandClass{name, relativeTo(inputs.root, file), lineAt(text, at)});
        });
    }
}

// `RAWPAGE_COMMAND(Class, "type.id", "header")`, and nothing else on the line. False when it is not.
bool parseRow(const std::string& code, TableRow& row)
{
    const std::string macro = "RAWPAGE_COMMAND";
    if (code.compare(0, macro.size(), macro) != 0) return false;
    std::size_t pos = skipSpace(code, macro.size());
    if (pos >= code.size() || code[pos] != '(') return false;
    pos = skipSpace(code, pos + 1);
    row.className = identifierAt(code, pos);
    if (row.className.empty()) return false;
    pos += row.className.size();

    for (std::string* field : {&row.typeId, &row.header}) {
        pos = skipSpace(code, pos);
        if (pos >= code.size() || code[pos] != ',') return false;
        pos = skipSpace(code, pos + 1);
        if (pos >= code.size() || code[pos] != '"') return false;
        const std::size_t close = code.find('"', pos + 1);
        if (close == std::string::npos) return false;
        *field = code.substr(pos + 1, close - pos - 1);
        pos = close + 1;
    }
    pos = skipSpace(code, pos);
    if (pos >= code.size() || code[pos] != ')') return false;
    return skipSpace(code, pos + 1) == code.size();
}

void scanTable(MutationInputs& inputs)
{
    const fs::path path = inputs.root / kTablePath;
    if (!fs::is_regular_file(path)) throw std::runtime_error("no command table at " + path.generic_string());

    std::istringstream lines(readFile(path));
    std::string line;
    int number = 0;
    while (std::getline(lines, line)) {
        ++number;
        const std::string code = trim(splitLine(line).code);
        if (code.empty() || code[0] == '#') continue;            // #ifndef / #define / #undef
        if (code.find("RAWPAGE_COMMAND") == std::string::npos) continue;
        TableRow row;
        if (!parseRow(code, row)) {
            // Exit 2, not a finding: a row this gate cannot read is a row it has not checked.
            throw std::runtime_error(path.generic_string() + ":" + std::to_string(number)
                                     + ": cannot read this row -- expected RAWPAGE_COMMAND(Class, \"type.id\", \"header\")");
        }
        row.line = number;
        inputs.rows.push_back(row);
    }
}

void scanTestTags(MutationInputs& inputs)
{
    const fs::path directory = inputs.root / kTestsDir;
    const fs::path excluded = inputs.root / kExcludedTestsDir;

    for (const fs::path& file : sourceFilesUnder(directory, fs::exists(excluded) ? excluded : fs::path{})) {
        ++inputs.testFilesScanned;
        const std::string text = withoutLineComments(readFile(file));
        // A tag counts only inside a test case's own macro arguments -- not in a comment, and not in an
        // INFO() or a string somewhere else in the file, which would claim a test that does not exist.
        forEachIdentifier(text, [&](const std::string& macro, std::size_t at) {
            if (macro != "TEST_CASE" && macro != "TEST_CASE_METHOD" && macro != "SCENARIO" && macro != "TEMPLATE_TEST_CASE")
                return;
            if (at > 0 && isIdentChar(text[at - 1])) return;
            const std::size_t open = skipSpace(text, at + macro.size());
            if (open >= text.size() || text[open] != '(') return;
            const std::size_t argsEnd = text.find('{', open);
            if (argsEnd == std::string::npos) return;
            const std::string args = text.substr(open + 1, argsEnd - open - 1);
            const TaggedTest where{relativeTo(inputs.root, file), lineAt(text, at)};

            for (const char* kind : {"serialize", "revert"}) {
                const std::string prefix = std::string("[") + kind + ":";
                for (std::size_t hit = args.find(prefix); hit != std::string::npos; hit = args.find(prefix, hit + 1)) {
                    const std::string name = identifierAt(args, hit + prefix.size());
                    const std::size_t close = hit + prefix.size() + name.size();
                    if (name.empty() || close >= args.size() || args[close] != ']') continue;
                    auto& map = std::string(kind) == "serialize" ? inputs.serializeTags : inputs.revertTags;
                    map[name].push_back(where);
                }
            }
        });
    }
}

} // namespace

MutationInputs loadMutationInputs(const fs::path& root)
{
    if (!fs::is_directory(root)) throw std::runtime_error("root is not a directory: " + root.generic_string());
    MutationInputs inputs;
    inputs.root = root;
    for (const char* required : {kCommandsDir, kTestsDir}) {
        if (!fs::is_directory(root / required))
            throw std::runtime_error("no " + std::string(required) + "/ under " + root.generic_string());
    }
    scanTable(inputs);
    scanCommandClasses(inputs);
    scanTestTags(inputs);
    return inputs;
}

std::vector<MutationFinding> checkMutations(const MutationInputs& inputs)
{
    std::vector<MutationFinding> findings;
    const std::string table = kTablePath;

    for (const CommandClass& command : inputs.classes) {
        const bool registered = std::any_of(inputs.rows.begin(), inputs.rows.end(),
                                            [&](const TableRow& row) { return row.className == command.name; });
        if (!registered) {
            findings.push_back({command.file, command.line, "unregistered-command",
                                command.name + " derives from Command and has no RAWPAGE_COMMAND row in " + table,
                                "CommandRegistry is generated from the table, so an unregistered command cannot be "
                                "deserialized: a crashed session that ran it cannot be recovered."});
        }
        if (inputs.serializeTags.find(command.name) == inputs.serializeTags.end()) {
            findings.push_back({command.file, command.line, "no-serialize-test",
                                command.name + " has no test case tagged [serialize:" + command.name + "] under tests/",
                                "The recovery journal writes every command and reads it back; a command whose "
                                "round trip is untested is a recovery that has never been tried."});
        }
        if (inputs.revertTags.find(command.name) == inputs.revertTags.end()) {
            findings.push_back({command.file, command.line, "no-revert-test",
                                command.name + " has no test case tagged [revert:" + command.name + "] under tests/",
                                "A new Command ships with its inverse and its tests, or it does not ship: undo "
                                "is only as good as the revert nobody measured."});
        }
    }

    for (const TableRow& row : inputs.rows) {
        const bool declared = std::any_of(inputs.classes.begin(), inputs.classes.end(), [&](const CommandClass& c) {
            return c.name == row.className && c.file == row.header;
        });
        if (!declared) {
            findings.push_back({table, row.line, "row-without-class",
                                row.className + " (\"" + row.typeId + "\") names header \"" + row.header
                                    + "\", which does not declare it as a Command under " + kCommandsDir + "/",
                                "The compiler never reads the header column -- CommandRegistry.cpp lists its "
                                "includes by hand -- so a wrong one is found here or nowhere."});
        }
    }

    std::stable_sort(findings.begin(), findings.end(), [](const MutationFinding& a, const MutationFinding& b) {
        return a.file != b.file ? a.file < b.file : a.line < b.line;
    });
    return findings;
}

int runMutationLint(const MutationInputs& inputs, std::ostream& out)
{
    const std::vector<MutationFinding> findings = checkMutations(inputs);

    std::set<std::string> explained;
    for (const MutationFinding& finding : findings) {
        out << finding.file << ":" << finding.line << ": " << finding.rule << ": " << finding.detail << "\n";
        if (explained.insert(finding.rule).second) out << "    why: " << finding.why << "\n";
    }

    if (!findings.empty()) {
        out << "\nmutation-lint: " << findings.size() << " finding(s)\n";
        return 1;
    }

    // The command roster, not just a count: "clean -- 0 commands" is how a gate that found nothing to
    // check would otherwise read as a gate that passed.
    out << "mutation-lint: clean -- " << inputs.classes.size() << " command(s):";
    for (const CommandClass& command : inputs.classes) out << " " << command.name;
    out << "; " << inputs.rows.size() << " table row(s); " << inputs.testFilesScanned << " test file(s) scanned\n";
    return 0;
}

void explainMutationLint(const MutationInputs& inputs, std::ostream& out)
{
    out << "inputs (relative to " << inputs.root.generic_string() << ")\n"
        << "  table     " << kTablePath << "\n"
        << "  commands  " << kCommandsDir << "/  (" << inputs.commandFilesScanned << " file(s))\n"
        << "  tests     " << kTestsDir << "/, except " << kExcludedTestsDir << "/  (" << inputs.testFilesScanned
        << " file(s))\n\n";

    out << "rules\n"
        << "  unregistered-command  a Command subclass under " << kCommandsDir << "/ with no table row\n"
        << "  no-serialize-test     ... with no TEST_CASE tagged [serialize:Class]\n"
        << "  no-revert-test        ... with no TEST_CASE tagged [revert:Class]\n"
        << "  row-without-class     a table row whose header does not declare its class as a Command\n\n";

    out << "NOT seen by this gate\n"
        << "  - a class deriving from Command through an intermediate base\n"
        << "  - a class declared by a macro, or inside a /* block comment */ (reported, falsely)\n"
        << "  - whether a tagged test tests anything: a lying revert is tst_reversibility's (guide A-44)\n\n";

    out << "commands\n";
    for (const CommandClass& command : inputs.classes) {
        const auto rowIt = std::find_if(inputs.rows.begin(), inputs.rows.end(),
                                        [&](const TableRow& row) { return row.className == command.name; });
        const auto serialize = inputs.serializeTags.find(command.name);
        const auto revert = inputs.revertTags.find(command.name);
        out << "  " << command.name << "  (" << command.file << ":" << command.line << ")\n"
            << "    row        " << (rowIt == inputs.rows.end() ? std::string("MISSING")
                                                                : "\"" + rowIt->typeId + "\", " + kTablePath + ":"
                                                                      + std::to_string(rowIt->line))
            << "\n"
            << "    serialize  " << (serialize == inputs.serializeTags.end() ? 0 : serialize->second.size())
            << " test case(s)\n"
            << "    revert     " << (revert == inputs.revertTags.end() ? 0 : revert->second.size()) << " test case(s)\n";
    }
}

} // namespace rawpage::tools
