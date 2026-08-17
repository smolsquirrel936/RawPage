#include "tools/layer-lint/LayerLint.h"

#include <algorithm>
#include <iostream>

namespace recto::tools {
namespace {

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool matchesAnyPrefix(const std::string& path, const std::vector<std::string>& prefixes) {
    for (const std::string& prefix : prefixes) {
        if (startsWith(path, prefix)) return true;
    }
    return false;
}

bool hasEscape(const SourceFile& file, const std::string& kind, int line) {
    for (const Escape& escape : file.escapes) {
        if (escape.kind == kind && escape.line == line) return true;
    }
    return false;
}

} // namespace

std::vector<Violation> check(const IncludeGraph& graph, const RuleSet& rules) {
    std::vector<Violation> violations;

    for (const SourceFile& file : graph.files()) {
        // A file outside every declared layer is unowned. That is not automatically a
        // violation -- build scripts and fixtures live outside the layer model -- but it
        // also means no include rule applies, so we simply skip it.
        if (file.layer == nullptr) continue;

        for (const Include& include : file.includes) {
            if (!include.resolved.empty()) {
                // Internal edge: the target is a file in this tree.
                const Layer* target = rules.layerFor(include.resolved);
                if (target == nullptr) continue;
                if (target == file.layer) continue;

                if (!matchesAnyPrefix(include.resolved, file.layer->mayInclude)) {
                    violations.push_back(Violation{
                        file.path, include.line, "outward-include",
                        file.layer->name + "/ includes " + include.resolved + " (" + target->name + "/)",
                        file.layer->note});
                }
            } else {
                // External header: enforced by deny-list, not allowlist.
                //
                // An allowlist would have to enumerate the whole C++ standard library and
                // would fail closed every time someone used a header nobody had listed --
                // which trains people to edit the rules file to get work done, and a rules
                // file that is edited casually stops meaning anything. The deny-list names
                // the headers that must never appear in a given layer, which is the actual
                // rule. The linker enforces the rest: core/ links only Qt6::Core, so an
                // errant QtWidgets include fails to link even if it slipped past here.
                for (const std::string& denied : file.layer->denyExternal) {
                    if (include.target.find(denied) != std::string::npos) {
                        violations.push_back(Violation{
                            file.path, include.line, "external-include",
                            file.layer->name + "/ includes <" + include.target + ">",
                            file.layer->note});
                        break;
                    }
                }
            }
        }

        for (const TokenHit& hit : file.tokens) {
            const TokenRule* rule = nullptr;
            for (const TokenRule& candidate : rules.tokenRules()) {
                if (candidate.kind == hit.kind) { rule = &candidate; break; }
            }
            if (rule == nullptr) continue;
            if (matchesAnyPrefix(file.path, rule->allowedPaths)) continue;
            if (rule->escapable && hasEscape(file, rule->kind, hit.line)) continue;

            violations.push_back(Violation{
                file.path, hit.line, rule->kind, "found '" + hit.pattern + "'", rule->message});
        }
    }

    std::sort(violations.begin(), violations.end(), [](const Violation& a, const Violation& b) {
        return a.file == b.file ? a.line < b.line : a.file < b.file;
    });
    return violations;
}

int run(const IncludeGraph& graph, const RuleSet& rules) {
    const std::vector<Violation> violations = check(graph, rules);

    if (violations.empty()) {
        std::cout << "layer-lint: clean -- " << graph.files().size() << " files, "
                  << graph.includeCount() << " includes\n";
        return 0;
    }

    std::string lastWhy;
    for (const Violation& v : violations) {
        std::cout << v.file << ":" << v.line << ": " << v.rule << ": " << v.detail << "\n";
        // The rationale is printed once per distinct rule rather than on every line: a
        // wall of the same paragraph repeated forty times is a wall people learn to skip.
        if (!v.why.empty() && v.why != lastWhy) {
            std::cout << "    why: " << v.why << "\n";
            lastWhy = v.why;
        }
    }
    std::cout << "\nlayer-lint: " << violations.size() << " violation(s)\n";
    return 1;
}

void explain(const IncludeGraph& graph, const RuleSet& rules, std::ostream& out, bool asDot) {
    if (asDot) {
        out << "digraph includes {\n  rankdir=LR;\n";
        for (const SourceFile& file : graph.files()) {
            for (const Include& include : file.includes) {
                if (include.resolved.empty()) continue;
                out << "  \"" << file.path << "\" -> \"" << include.resolved << "\";\n";
            }
        }
        out << "}\n";
        return;
    }

    out << "rules: " << rules.sourcePath().generic_string() << "\n\n";

    out << "layers\n";
    for (const Layer& layer : rules.layers()) {
        out << "  " << layer.name << "\n    paths:      ";
        for (const std::string& p : layer.paths) out << p << " ";
        out << "\n    mayInclude: ";
        for (const std::string& p : layer.mayInclude) out << p << " ";
        if (!layer.denyExternal.empty()) {
            out << "\n    denyExternal: ";
            for (const std::string& p : layer.denyExternal) out << p << " ";
        }
        out << "\n";
    }

    out << "\nexcluded paths (NOT scanned)\n";
    for (const std::string& p : rules.excludedPaths()) out << "  " << p << "\n";

    out << "\ntoken rules\n";
    for (const TokenRule& rule : rules.tokenRules()) {
        out << "  " << rule.kind << (rule.escapable ? "  [escapable]" : "  [no escape]") << "\n";
        out << "    allowed in: ";
        for (const std::string& p : rule.allowedPaths) out << p << " ";
        out << "\n";
    }

    // Every live escape is listed, always. An escape hatch nobody can see is an escape
    // hatch that quietly becomes the norm.
    out << "\nactive escapes\n";
    size_t escapeCount = 0;
    for (const SourceFile& file : graph.files()) {
        for (const Escape& escape : file.escapes) {
            out << "  " << file.path << ":" << escape.line << ": allow-" << escape.kind
                << " -- " << (escape.reason.empty() ? "(no reason given)" : escape.reason) << "\n";
            ++escapeCount;
        }
    }
    if (escapeCount == 0) out << "  (none)\n";

    out << "\ninclude graph\n";
    for (const SourceFile& file : graph.files()) {
        if (file.includes.empty()) continue;
        out << "  " << file.path << "  [" << (file.layer ? file.layer->name : "unowned") << "]\n";
        for (const Include& include : file.includes) {
            out << "      " << (include.angled ? "<" : "\"") << include.target
                << (include.angled ? ">" : "\"");
            if (!include.resolved.empty()) out << "  -> " << include.resolved;
            else out << "  -> (external)";
            out << "\n";
        }
    }

    out << "\n" << graph.files().size() << " files, " << graph.includeCount() << " includes, "
        << escapeCount << " escapes\n";
}

} // namespace recto::tools
