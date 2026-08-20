#ifndef RAWPAGE_LAYER_LINT_LAYERLINT_H
#define RAWPAGE_LAYER_LINT_LAYERLINT_H

#include "tools/layer-lint/IncludeGraph.h"
#include "tools/layer-lint/RuleSet.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace rawpage::tools {

struct Violation {
    std::string file;
    int line = 0;
    std::string rule;      // stable id: "outward-include", "external-include", "fpdf", ...
    std::string detail;    // what was found
    std::string why;       // the layer's or rule's note -- a violation should teach, not just fail
};

// The check itself, separated from any printing so it can be exercised directly.
std::vector<Violation> check(const IncludeGraph& graph, const RuleSet& rules);

// The entry point named in the Stage 0 plan. Prints violations and returns the process
// exit code: 0 clean, 1 violations found.
int run(const IncludeGraph& graph, const RuleSet& rules);

// --explain: the include graph, the active rules, and every live escape.
void explain(const IncludeGraph& graph, const RuleSet& rules, std::ostream& out, bool asDot);

} // namespace rawpage::tools

#endif // RAWPAGE_LAYER_LINT_LAYERLINT_H
