// tools/mutation-lint -- implementation. Stage 3 T6. See the header for the rules and the blind spots.

#include "tools/mutation-lint/MutationLint.h"

#include "tools/common/LineScan.h"

#include <algorithm>
#include <fstream>
#include <ostream>
#include <regex>
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

void scanCommandClasses(MutationInputs& inputs)
{
    const fs::path directory = inputs.root / kCommandsDir;
    // `class Name [final] :` -- then the base list, up to the body or the end of the declaration.
    static const std::regex head(R"(\b(?:class|struct)\s+([A-Za-z_]\w*)(?:\s+final)?\s*:(?!:))");
    // One base in that list that is `Command`, however qualified and however many bases sit beside it.
    static const std::regex commandBase(
        R"((?:^|,)\s*(?:(?:public|protected|private|virtual)\s+)*(?:(?:::)?\s*[A-Za-z_]\w*\s*::\s*)*(?:::\s*)?Command\s*(?:,|$))");

    for (const fs::path& file : sourceFilesUnder(directory, {})) {
        ++inputs.commandFilesScanned;
        const std::string text = withoutLineComments(readFile(file));
        for (auto it = std::sregex_iterator(text.begin(), text.end(), head); it != std::sregex_iterator(); ++it) {
            const std::size_t baseStart = static_cast<std::size_t>(it->position(0) + it->length(0));
            const std::size_t baseEnd = text.find_first_of("{;", baseStart);
            if (baseEnd == std::string::npos) continue;
            std::string bases = text.substr(baseStart, baseEnd - baseStart);
            std::replace(bases.begin(), bases.end(), '\n', ' ');
            if (!std::regex_search(bases, commandBase)) continue;
            inputs.classes.push_back(CommandClass{(*it)[1].str(), relativeTo(inputs.root, file),
                                                  lineAt(text, static_cast<std::size_t>(it->position(0)))});
        }
    }
}

void scanTable(MutationInputs& inputs)
{
    const fs::path path = inputs.root / kTablePath;
    if (!fs::is_regular_file(path)) throw std::runtime_error("no command table at " + path.generic_string());

    static const std::regex row(
        R"re(^\s*RAWPAGE_COMMAND\s*\(\s*([A-Za-z_]\w*)\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)\s*$)re");
    std::istringstream lines(readFile(path));
    std::string line;
    int number = 0;
    while (std::getline(lines, line)) {
        ++number;
        const std::string code = trim(splitLine(line).code);
        if (code.empty() || code[0] == '#') continue;            // #ifndef / #define / #undef
        if (code.find("RAWPAGE_COMMAND") == std::string::npos) continue;
        std::smatch match;
        if (!std::regex_match(code, match, row)) {
            // Exit 2, not a finding: a row this gate cannot read is a row it has not checked.
            throw std::runtime_error(path.generic_string() + ":" + std::to_string(number)
                                     + ": cannot read this row -- expected RAWPAGE_COMMAND(Class, \"type.id\", \"header\")");
        }
        inputs.rows.push_back(TableRow{match[1].str(), match[2].str(), match[3].str(), number});
    }
}

void scanTestTags(MutationInputs& inputs)
{
    const fs::path directory = inputs.root / kTestsDir;
    const fs::path excluded = inputs.root / kExcludedTestsDir;
    // A tag counts only inside a test case's own macro arguments -- not in a comment, and not in an
    // INFO() or a string somewhere else in the file, which would claim a test that does not exist.
    static const std::regex testCase(R"(\b(?:TEST_CASE_METHOD|TEST_CASE|SCENARIO|TEMPLATE_TEST_CASE)\s*\()");
    static const std::regex tag(R"(\[(serialize|revert):([A-Za-z_]\w*)\])");

    for (const fs::path& file : sourceFilesUnder(directory, fs::exists(excluded) ? excluded : fs::path{})) {
        ++inputs.testFilesScanned;
        const std::string text = withoutLineComments(readFile(file));
        for (auto it = std::sregex_iterator(text.begin(), text.end(), testCase); it != std::sregex_iterator(); ++it) {
            const std::size_t argsStart = static_cast<std::size_t>(it->position(0) + it->length(0));
            const std::size_t argsEnd = text.find('{', argsStart);
            if (argsEnd == std::string::npos) continue;
            const std::string args = text.substr(argsStart, argsEnd - argsStart);
            const TaggedTest where{relativeTo(inputs.root, file), lineAt(text, static_cast<std::size_t>(it->position(0)))};
            for (auto t = std::sregex_iterator(args.begin(), args.end(), tag); t != std::sregex_iterator(); ++t) {
                auto& map = (*t)[1].str() == "serialize" ? inputs.serializeTags : inputs.revertTags;
                map[(*t)[2].str()].push_back(where);
            }
        }
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
