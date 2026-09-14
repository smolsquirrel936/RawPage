// tools/mutation-lint -- the fourth enforcement gate. Stage 3 T6, decision D3-12
// (`Stage3-Mutation-Plan.md` §3).
//
// THE RULE IT ENFORCES: *a new `Command` ships with its inverse and its tests, or it does not ship.*
// For every class deriving from `Command` under `core/cmd/commands/`:
//
//   unregistered-command   no `RAWPAGE_COMMAND(Class, ...)` row in `core/cmd/Commands.def`
//   no-serialize-test      no Catch2 test case under `tests/` tagged `[serialize:Class]`
//   no-revert-test         no Catch2 test case under `tests/` tagged `[revert:Class]`
//
// and for every table row:
//
//   row-without-class      the row's header column does not name a file under `core/cmd/commands/`
//                          that declares the row's class as a `Command`. The compiler never reads
//                          that column -- `CommandRegistry.cpp` lists its includes by hand -- so this
//                          gate is the only thing that keeps it true.
//
// EVERYTHING IS READ AS TEXT, with the standard library only (D0-1), so `checks` runs this on the bare
// `gates` runner before anything is compiled -- and it reads the same `.def` the compiler reads
// (convention 8).
//
// WHAT IT CANNOT SEE, and `--explain` prints this list so nobody has to read the tool to learn it:
//   * a class deriving from `Command` INDIRECTLY, through an intermediate base -- only
//     `: public Command` (any namespace qualification) is recognized;
//   * a class declared by a macro;
//   * whether a tagged test TESTS anything. The gate proves a test exists. A revert that lies is
//     `tests/unit/tst_reversibility.cpp`'s to catch (guide case A-44), which is why D3-12 names two
//     mechanisms.
//
// `tests/gates/` is not scanned for tags: it holds this gate's own fixtures, whose tags name fixture
// classes, and a gate fixture is not a test of the product.

#ifndef RAWPAGE_MUTATION_LINT_MUTATIONLINT_H
#define RAWPAGE_MUTATION_LINT_MUTATIONLINT_H

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rawpage::tools {

struct CommandClass {
    std::string name;
    std::string file;      // relative to the root, '/'-separated
    int         line = 0;
};

struct TableRow {
    std::string className;
    std::string typeId;
    std::string header;    // as written in the row
    int         line = 0;
};

struct TaggedTest {
    std::string file;
    int         line = 0;
};

struct MutationInputs {
    std::filesystem::path     root;
    std::vector<CommandClass> classes;
    std::vector<TableRow>     rows;
    std::map<std::string, std::vector<TaggedTest>> serializeTags;   // class -> test cases
    std::map<std::string, std::vector<TaggedTest>> revertTags;
    std::size_t commandFilesScanned = 0;
    std::size_t testFilesScanned = 0;
};

struct MutationFinding {
    std::string file;
    int         line = 0;
    std::string rule;
    std::string detail;
    std::string why;
};

// Throws std::runtime_error -- exit 2 in main() -- when the table, the commands directory or the tests
// directory is absent, or a table row cannot be parsed. A gate that could not read its inputs has not
// run, and must not look like one that passed.
MutationInputs loadMutationInputs(const std::filesystem::path& root);

std::vector<MutationFinding> checkMutations(const MutationInputs& inputs);

// 0 clean, 1 findings.
int runMutationLint(const MutationInputs& inputs, std::ostream& out);

void explainMutationLint(const MutationInputs& inputs, std::ostream& out);

} // namespace rawpage::tools

#endif // RAWPAGE_MUTATION_LINT_MUTATIONLINT_H
