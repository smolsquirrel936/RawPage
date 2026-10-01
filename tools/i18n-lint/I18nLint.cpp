#include "tools/i18n-lint/I18nLint.h"

#include <algorithm>
#include <ostream>
#include <set>

namespace fs = std::filesystem;

namespace rawpage::tools {
namespace {

const char* kWhyLiteral =
    "A literal that reaches a user is a string the product cannot ship in zh-TW. Wrap it in tr(), "
    "or -- if this really is not user-facing -- waive it inline with "
    "// i18n-lint: allow-literal(reason), which --explain censuses.";

const char* kWhyMissingKey =
    "A key present in the source catalog and absent from a SHIPPED locale renders as nothing, or "
    "as the key itself, on a user's screen. A locale that is not ready is declared "
    "\"shipped\": false (D1-14) rather than left incomplete.";

const char* kWhyOrphanKey =
    "A catalog key nothing references is usually a key whose call site was removed. Reported and "
    "never blocking, because a key may legitimately lead its use by a commit. A quoted reference "
    "ENDING IN A DOT is a namespace reference and covers every key beneath it: a generated key "
    "(prefs.context.Viewer, built from a KeyContext name) never appears whole in any source, and "
    "reporting forty of those every run is how a note nobody can act on teaches people to skip the "
    "whole rule.";

const char* kWhyUnknownKey =
    "A source names a catalog key that no shipped catalog defines. The key is what ui/ looks up, so "
    "the surface renders the key text itself -- \"error.font.noSubstitute\" in a message bar. This is "
    "the INVERSE of orphan-key and neither rule sees what the other does: orphan-key asks whether a "
    "catalog entry is referenced, missing-key compares the catalogs against each other, and "
    "action-label covers only the action.* family. Issue I-04.";

const char* kWhyWrongContext =
    "A catalog entry is looked up in the context named by its key's first segment -- ui/'s "
    "catalog::translate asks QCoreApplication::translate(\"search\", \"search.bar.count\") -- so an entry "
    "written inside another <context> is compiled into the .qm and found by nothing, and the surface "
    "shows the key. Every other rule passes it: the key exists (unknown-key), every locale has it "
    "(missing-key), and something references it (orphan-key). Stage 5 T7 put sixteen search.* entries "
    "in the prefs context and the find bar read search.bar.count; S5 guide I-05.";

const char* kWhyActionLabel =
    "An ActionId with no action.* catalog entry renders as a button with no label. ActionId.def "
    "carries the catalog key as its own column precisely so this can be checked without a compiler.";

} // namespace

Inputs load(const fs::path& root, const fs::path& rulesPath, const fs::path& localesPath,
            bool localesExplicit, const fs::path& actionDefPath, bool actionDefExplicit)
{
    Inputs inputs{root, I18nRules::loadFromFile(rulesPath), std::nullopt, actionDefPath, false, {}, {}};

    inputs.sources = scanSources(root, inputs.rules);

    if (fs::exists(localesPath)) {
        inputs.locales = LocaleSet::loadFromFile(localesPath);
        for (const LocaleDeclaration& locale : inputs.locales->locales()) {
            const fs::path catalogPath = root / locale.catalog;
            if (!fs::exists(catalogPath)) {
                if (locale.shipped) {
                    // A shipped locale whose catalog is missing is not a warning. It is the state
                    // the missing-key rule exists to prevent, arrived at by a different route.
                    throw std::runtime_error("locale '" + locale.code
                                             + "' is declared shipped but its catalog is missing: "
                                             + catalogPath.string());
                }
                continue;
            }
            inputs.catalogs.emplace_back(locale, Catalog::loadFromFile(catalogPath));
        }
    } else if (localesExplicit) {
        throw std::runtime_error("cannot open locales file: " + localesPath.string());
    }

    if (fs::exists(actionDefPath)) {
        inputs.actionDefPresent = true;
    } else if (actionDefExplicit) {
        throw std::runtime_error("cannot open action table: " + actionDefPath.string());
    }

    return inputs;
}

std::vector<Finding> check(const Inputs& inputs)
{
    std::vector<Finding> findings;

    // --- unwrapped-literal --------------------------------------------------------------------
    for (const LiteralHit& hit : inputs.sources.literals) {
        findings.push_back(Finding{hit.file, hit.line, "unwrapped-literal",
                                   "literal \"" + hit.literal + "\" reaches the user through "
                                       + hit.sink + ")",
                                   kWhyLiteral, true});
    }

    const Catalog* source = nullptr;
    std::string sourceCode;
    if (inputs.locales) {
        sourceCode = inputs.locales->sourceLocale();
        for (const auto& [locale, catalog] : inputs.catalogs) {
            if (locale.code == sourceCode) { source = &catalog; break; }
        }
    }

    if (source != nullptr) {
        // --- missing-key ----------------------------------------------------------------------
        for (const auto& [locale, catalog] : inputs.catalogs) {
            if (!locale.shipped || locale.code == sourceCode) continue;
            for (const CatalogEntry& entry : source->entries()) {
                if (catalog.contains(entry.key)) continue;
                findings.push_back(Finding{catalog.path().generic_string(), 0, "missing-key",
                                           "locale " + locale.code + " has no entry for "
                                               + entry.key,
                                           kWhyMissingKey, true});
            }
        }

        // A duplicate key means one of the two spellings silently wins. Blocking, because which one
        // wins depends on parse order and nothing else.
        for (const auto& [locale, catalog] : inputs.catalogs) {
            for (const std::string& key : catalog.duplicateKeys()) {
                findings.push_back(Finding{catalog.path().generic_string(), 0, "missing-key",
                                           "locale " + locale.code + " declares " + key
                                               + " more than once",
                                           kWhyMissingKey, true});
            }
        }

        // --- wrong-context (BLOCKING) ----------------------------------------------------------
        // Every catalog, shipped or not: an entry in the wrong context is a defect in any of them, and
        // an unshipped locale is the one a translator is filling in today.
        for (const auto& [locale, catalog] : inputs.catalogs) {
            for (const CatalogEntry& entry : catalog.entries()) {
                const std::size_t dot = entry.key.find('.');
                if (dot == std::string::npos) continue;   // no family: unknown-key's and translate's concern
                const std::string family = entry.key.substr(0, dot);
                if (family == entry.context) continue;
                findings.push_back(Finding{catalog.path().generic_string(), entry.line, "wrong-context",
                                           entry.key + " is in context " + entry.context + ", not " + family,
                                           kWhyWrongContext, true});
            }
        }

        // --- action-label ---------------------------------------------------------------------
        if (inputs.actionDefPresent) {
            for (const std::string& key : actionLabelKeys(inputs.actionDefPath)) {
                if (source->contains(key)) continue;
                findings.push_back(Finding{inputs.actionDefPath.generic_string(), 0, "action-label",
                                           "no catalog entry for " + key, kWhyActionLabel, true});
            }
        }

        // --- unknown-key (BLOCKING) -----------------------------------------------------------
        //
        // The question issue I-04 found that nothing asked: does every catalog key a SOURCE names
        // actually exist? It is anchored on the declared families rather than on every dotted
        // literal, because the scanner's notion of a key is deliberately generous for orphan-key's
        // sake and a blocking rule over that set would fail on settings.ini and pdfium.dll.
        //
        // It is checked against the SOURCE catalog (`en`), which is the one the key IS: missing-key
        // already reports a key present in en and absent from a shipped locale, so checking every
        // catalog here would report each gap twice under two rule names.
        std::set<std::string> unknown;   // deduped: one finding per key, not one per call site
        for (const KeyReference& reference : inputs.sources.keyReferences) {
            if (!inputs.rules.isCatalogKey(reference.key)) continue;
            if (source->contains(reference.key)) continue;
            if (!unknown.insert(reference.key).second) continue;

            findings.push_back(Finding{reference.file, reference.line, "unknown-key",
                                       reference.key + " is named by this source and defined by no "
                                                       "catalog",
                                       kWhyUnknownKey, true});
        }

        // --- orphan-key (S3: reported, never blocking) -----------------------------------------
        std::set<std::string> referenced;

        // A reference ending in a dot names a NAMESPACE rather than a key. Several catalog keys in
        // this product are built at runtime from a table -- one per KeyContext, one per settings
        // path, one per log subsystem -- so the whole key is never a literal anywhere and only the
        // prefix is. See kWhyOrphanKey.
        std::set<std::string> referencedNamespaces;

        for (const KeyReference& reference : inputs.sources.keyReferences) {
            referenced.insert(reference.key);
            if (!reference.key.empty() && reference.key.back() == '.') {
                referencedNamespaces.insert(reference.key);
            }
        }

        const auto coveredByNamespace = [&referencedNamespaces](const std::string& key) {
            for (const std::string& prefix : referencedNamespaces) {
                if (key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0) {
                    return true;
                }
            }
            return false;
        };
        if (inputs.actionDefPresent) {
            for (const std::string& key : actionLabelKeys(inputs.actionDefPath)) referenced.insert(key);
        }
        for (const CatalogEntry& entry : source->entries()) {
            if (referenced.count(entry.key) != 0) continue;
            if (coveredByNamespace(entry.key)) continue;
            findings.push_back(Finding{source->path().generic_string(), entry.line, "orphan-key",
                                       entry.key + " is referenced by no source file", kWhyOrphanKey,
                                       false});
        }
    }

    std::stable_sort(findings.begin(), findings.end(), [](const Finding& a, const Finding& b) {
        if (a.blocking != b.blocking) return a.blocking > b.blocking;
        if (a.file != b.file) return a.file < b.file;
        return a.line < b.line;
    });
    return findings;
}

int run(const Inputs& inputs, std::ostream& out)
{
    const std::vector<Finding> findings = check(inputs);

    std::size_t blocking = 0;
    std::string lastWhy;
    for (const Finding& finding : findings) {
        if (finding.blocking) ++blocking;
        out << finding.file;
        if (finding.line > 0) out << ":" << finding.line;
        out << ": " << (finding.blocking ? "" : "note: ") << finding.rule << ": " << finding.detail
            << "\n";
        // The rationale once per distinct rule, not on every line: a wall of the same paragraph
        // repeated forty times is a wall people learn to skip.
        if (!finding.why.empty() && finding.why != lastWhy) {
            out << "    why: " << finding.why << "\n";
            lastWhy = finding.why;
        }
    }

    // Said out loud whether or not anything failed, because "which rules actually ran" is the
    // difference between a clean tree and a gate that had nothing to check.
    if (!inputs.locales) {
        out << "i18n-lint: NO LOCALES FILE under this root -- only the unwrapped-literal rule ran\n";
    }
    if (inputs.locales && !inputs.actionDefPresent) {
        out << "i18n-lint: NO ActionId.def under this root -- the action-label rule did not run\n";
    }

    if (blocking == 0) {
        out << "i18n-lint: clean -- " << inputs.sources.filesScanned << " files, "
            << inputs.catalogs.size() << " catalog(s), " << inputs.sources.waivers.size()
            << " waiver(s)";
        if (findings.size() > blocking) {
            out << ", " << (findings.size() - blocking) << " note(s)";
        }
        out << "\n";
        return 0;
    }

    out << "\ni18n-lint: " << blocking << " finding(s)\n";
    return 1;
}

void explain(const Inputs& inputs, std::ostream& out)
{
    out << "rules: " << inputs.rules.sourcePath().generic_string() << "\n\n";

    out << "scanned paths\n  ";
    for (const std::string& path : inputs.rules.scannedPaths()) out << path << " ";
    out << "\n\nexcluded paths (NOT scanned)\n";
    for (const std::string& path : inputs.rules.excludedPaths()) out << "  " << path << "\n";

    out << "\nuser-facing sinks -- a literal reaching one of these is the defect\n";
    for (const std::string& sink : inputs.rules.sinks()) out << "  " << sink << ")\n";

    out << "\nwrappers -- a literal inside one of these is legitimate\n";
    for (const std::string& wrapper : inputs.rules.wrappers()) out << "  " << wrapper << ")\n";

    // The unknown-key rule is anchored entirely on this list, so a family missing from it is a
    // namespace nothing checks -- a silent gap, and therefore one --explain has to show. Same
    // argument as the waiver census: a rule's blind spot must be visible without reading the tool.
    out << "\ncatalog families -- unknown-key checks a reference only if it starts with one\n  ";
    for (const std::string& family : inputs.rules.keyFamilies()) out << family << ".* ";
    out << "\n";

    // D1-14 made visible. A silent skip is indistinguishable from a forgotten locale, which is
    // exactly the distinction that decision exists to keep in view.
    out << "\ndeclared locales\n";
    if (!inputs.locales) {
        out << "  (no locales file under this root)\n";
    } else {
        out << "  source locale: " << inputs.locales->sourceLocale() << "\n";
        for (const LocaleDeclaration& locale : inputs.locales->locales()) {
            out << "  " << locale.code << "  " << (locale.shipped ? "SHIPPED" : "declared, NOT shipped");
            if (!locale.shipped) {
                out << " -- "
                    << (locale.plannedFor.empty() ? std::string("no plan recorded") : locale.plannedFor);
            }
            out << "\n      catalog: " << locale.catalog;

            const Catalog* loaded = nullptr;
            for (const auto& [declared, catalog] : inputs.catalogs) {
                if (declared.code == locale.code) { loaded = &catalog; break; }
            }
            if (loaded == nullptr) {
                out << "  (not present)\n";
            } else {
                out << "\n      " << loaded->entries().size() << " key(s), "
                    << loaded->unfinishedCount() << " marked unfinished\n";
            }
        }
    }

    // Unfinished is NOT the same as missing, and conflating them is how a machine-seeded catalog
    // gets reported as a reviewed one. `missing-key` fails; this counts.
    out << "\nunfinished translations -- present, keyed, and not yet reviewed by a person\n";
    bool sawUnfinished = false;
    for (const auto& [locale, catalog] : inputs.catalogs) {
        const std::size_t count = catalog.unfinishedCount();
        if (count == 0) continue;
        sawUnfinished = true;
        out << "  " << locale.code << ": " << count << " of " << catalog.entries().size() << "\n";
    }
    if (!sawUnfinished) out << "  (none)\n";

    // Every live waiver, always, and APPLIED ones counted separately -- I-20's finding transplanted
    // before it could happen again here. A waiver that silenced nothing is not a waiver; counting
    // it would overstate the census that makes escapability safe.
    out << "\nactive waivers\n";
    std::size_t applied = 0;
    for (const WaiverHit& waiver : inputs.sources.waivers) {
        out << "  " << waiver.file << ":" << waiver.line << ": allow-" << waiver.kind << " -- "
            << (waiver.reason.empty() ? "(no reason given)" : waiver.reason)
            << (waiver.applied ? "  [applied]" : "  [silenced nothing]") << "\n";
        if (waiver.applied) ++applied;
    }
    if (inputs.sources.waivers.empty()) out << "  (none)\n";

    out << "\n" << inputs.sources.filesScanned << " files scanned, "
        << inputs.sources.literalsConsidered << " literal(s) at a sink, "
        << inputs.sources.waivers.size() << " waiver(s) of which " << applied << " applied\n";
}

} // namespace rawpage::tools
