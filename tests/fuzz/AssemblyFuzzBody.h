// The body of `fuzz_assembly` -- Stage 7 T5, decision D7-23 (`Stage7-Assembly-Plan.md` §3) -- in a header so the
// libFuzzer target and the replay in `tests/unit/tst_copier.cpp` share exactly one copy, the arrangement
// `RoundtripFuzzBody.h` and `AnnotPipelineFuzzBody.h` document.
//
// -------------------------------------------------------------------------------------------
// THE PIPELINE, AS FAR AS T5 BUILDS IT
// -------------------------------------------------------------------------------------------
// bytes -> `pdf/cos/` (`openWithRepair`) -> the page walk -> `ObjectGraphCopier` (every page, up to a bound, into
// a payload) -> the payload re-read as a file of its own -> the payload INSERTED into a fixed one-page target and
// one of its pages REPLACING the target's, each composed by `RevisionWriter` and VERIFIED as a save would, then
// undone. T7 adds the assembler (`DocumentAssembler`), T11 the outline reader and T17 the layer tree over the same
// bytes (D7-23).
//
// THE INVARIANTS, each one a defect the copier or the sink could plausibly have:
//   * REFUSAL  -- a copy refused is refused BY KEY, one of the copier's own; anything else is a finding;
//   * PAYLOAD  -- a copy made is a PDF the first-party reader opens with no repair, whose walk finds exactly
//                 the pages copied, each carrying `/Resources`, `/MediaBox` and `/Rotate` of its own;
//   * TREE     -- the payload holds one catalog and one page-tree node, its own: nothing of the source's tree
//                 came along (D7-4's stop);
//   * CLOSED   -- every reference in the payload names an object the payload holds, or nothing at all;
//   * INSERT   -- the payload inserted into the target composes to an update `RevisionWriter::verify` accepts,
//                 with the target's page count plus the payload's, and an undo leaves nothing to write;
//   * REPLACE  -- the payload's first page replacing the target's page does the same, the count unchanged.
//
// WHAT IS NOT A FINDING: an input the walk cannot read, or one the copier refuses by key.

#ifndef RAWPAGE_TESTS_FUZZ_ASSEMBLYFUZZBODY_H
#define RAWPAGE_TESTS_FUZZ_ASSEMBLYFUZZBODY_H

#include "core/doc/CopiedPages.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/RevisionWriter.h"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace rawpage::fuzz {

struct AssemblyVerdict {
    bool ok = true;
    std::string failure;
    bool copied = false;   // the input reached the copy -- what the replay counts, so a corpus that reaches nothing shows
};

inline AssemblyVerdict assemblyFail(std::string why)
{
    AssemblyVerdict verdict;
    verdict.ok = false;
    verdict.failure = std::move(why);
    return verdict;
}

// Pages copied from one input, at most; and the copy's budget -- both so one input is a bounded amount of work.
inline constexpr int kFuzzMaxPages = 32;

namespace detail {

inline bool referencesClosed(pdf::cos::XrefTable& table, const pdf::cos::CosObject& value, int depth = 0)
{
    if (depth > 200) return true;
    if (value.isReference()) return table.find(value.reference().number) != nullptr;
    for (const pdf::cos::CosObject& item : value.items()) {
        if (!referencesClosed(table, item, depth + 1)) return false;
    }
    for (const pdf::cos::CosObject::Entry& entry : value.entries()) {
        if (!referencesClosed(table, entry.value, depth + 1)) return false;
    }
    return true;
}

// The target every input's pages go into: one blank page, the blank payload being a well-formed file of its own.
inline const std::string& target()
{
    static const std::string bytes = [] {
        auto blank = pdf::ops::ObjectGraphCopier::blank(612.0, 792.0, 1);
        return blank ? blank.value().pdf : std::string();
    }();
    return bytes;
}

inline std::string composedAndVerified(pdf::ops::MutationSinkImpl& sink, int pages)
{
    auto composed = pdf::ops::RevisionWriter::compose(sink, false);
    if (!composed) return std::string("compose refused: ") + composed.error().messageKey.data() + " "
                          + composed.error().context.toStdString();
    const core::Status verified = pdf::ops::RevisionWriter::verify(
        sink.bytes(), std::string_view(composed.value().bytes.constData(), static_cast<std::size_t>(composed.value().bytes.size())), sink);
    if (!verified) return std::string("verify refused: ") + verified.error().context.toStdString();
    if (sink.pageCount() != pages) return "page count " + std::to_string(sink.pageCount());
    return std::string();
}

} // namespace detail

inline AssemblyVerdict assemblyFuzzOnce(const std::uint8_t* data, std::size_t size)
{
    AssemblyVerdict verdict;
    const std::string_view bytes(reinterpret_cast<const char*>(data), size);
    auto opened = pdf::cos::openWithRepair(bytes);
    if (!opened) return verdict;
    auto walk = pdf::ops::PageOps::walk(opened.value().table);
    if (!walk || walk.value().pages.empty()) return verdict;
    std::vector<int> pages;
    for (int i = 0; i < static_cast<int>(walk.value().pages.size()) && i < kFuzzMaxPages; ++i) pages.push_back(i);

    pdf::ops::ObjectGraphCopier::Options options;
    options.maxObjects = 20'000;
    options.maxBytes = 16u * 1024u * 1024u;
    auto copied = pdf::ops::ObjectGraphCopier::copy(opened.value().table, walk.value(), pages, options);
    if (!copied) {
        static const std::set<std::string> known{"error.pdf.encrypted", "error.pdf.pageTreeDamaged", "error.copy.tooLarge",
                                                 "error.pdf.pageOutOfRange"};
        const std::string key(copied.error().messageKey.data(), static_cast<std::size_t>(copied.error().messageKey.size()));
        return known.count(key) != 0 ? verdict : assemblyFail("copy refused by an unexpected key: " + key);
    }
    verdict.copied = true;

    // PAYLOAD, TREE, CLOSED -- read as a file of its own.
    const std::string& pdf = copied.value().pdf;
    auto reread = pdf::cos::openWithRepair(pdf);
    if (!reread) return assemblyFail("the payload does not open");
    if (!reread.value().usedOriginalXref || reread.value().report.requiresSaveAs()) return assemblyFail("the payload needed a repair");
    pdf::cos::XrefTable& table = reread.value().table;
    auto copiedWalk = pdf::ops::PageOps::walk(table);
    if (!copiedWalk || copiedWalk.value().pages.size() != pages.size() || !copiedWalk.value().notes.empty())
        return assemblyFail("the payload's pages are not the pages copied");
    int catalogs = 0;
    int nodes = 0;
    for (const auto& [number, entry] : table.entries()) {
        if (entry.type == pdf::cos::XrefEntryType::Free || number == 0) continue;
        auto object = table.fetch(number);
        if (!object) return assemblyFail("payload object " + std::to_string(number) + " does not fetch");
        const pdf::cos::CosObject& value = object.value().object;
        const pdf::cos::CosObject& type = value.get("Type");
        if (type.isName() && type.text() == "Catalog") ++catalogs;
        if (type.isName() && type.text() == "Pages") ++nodes;
        if (!detail::referencesClosed(table, value)) return assemblyFail("payload object " + std::to_string(number) + " names an object it lacks");
    }
    if (catalogs != 1 || nodes != 1) return assemblyFail("the payload holds the source's tree");
    for (const pdf::ops::PageEntry& page : copiedWalk.value().pages) {
        auto object = table.fetch(page.number);
        if (!object || !object.value().object.has("Resources") || !object.value().object.has("MediaBox")
            || !object.value().object.has("Rotate"))
            return assemblyFail("a copied page lacks an inheritable key of its own");
    }

    // INSERT -- into the fixed target, composed and verified, then undone.
    const std::string& into = detail::target();
    if (into.empty()) return assemblyFail("the target could not be made");
    const QByteArray graph(pdf.data(), static_cast<qsizetype>(pdf.size()));
    {
        auto sink = pdf::ops::MutationSinkImpl::open(into);
        if (!sink) return assemblyFail("the target does not open");
        const QString key = QStringLiteral("0123456789abcdef0123456789abcdef");
        auto inserted = sink.value()->insertPages(1, key, graph);
        if (!inserted) return assemblyFail(std::string("insert refused: ") + inserted.error().messageKey.data());
        if (const std::string why = detail::composedAndVerified(*sink.value(), 1 + static_cast<int>(pages.size())); !why.empty())
            return assemblyFail("insert: " + why);
        std::vector<int> added;
        for (std::size_t i = 0; i < pages.size(); ++i) added.push_back(1 + static_cast<int>(i));
        if (!sink.value()->removePages(added, key)) return assemblyFail("the insert's undo was refused");
        auto delta = sink.value()->delta();
        if (!delta || !delta.value().isEmpty()) return assemblyFail("an insert undone still writes");
    }
    // REPLACE -- the payload's first page over the target's, composed and verified.
    {
        auto one = pdf::ops::ObjectGraphCopier::copy(opened.value().table, walk.value(), {0}, options);
        if (!one) return assemblyFail("a copy of one page was refused where the copy of all was not");
        auto sink = pdf::ops::MutationSinkImpl::open(into);
        if (!sink) return assemblyFail("the target does not open");
        const QString key = QStringLiteral("fedcba9876543210fedcba9876543210");
        auto replaced = sink.value()->replacePages({0}, key, QByteArray(one.value().pdf.data(), static_cast<qsizetype>(one.value().pdf.size())));
        if (!replaced) return assemblyFail(std::string("replace refused: ") + replaced.error().messageKey.data());
        if (const std::string why = detail::composedAndVerified(*sink.value(), 1); !why.empty()) return assemblyFail("replace: " + why);
        if (!sink.value()->restoreReplacedPages({0}, key)) return assemblyFail("the replace's undo was refused");
        auto delta = sink.value()->delta();
        if (!delta || !delta.value().isEmpty()) return assemblyFail("a replace undone still writes");
    }
    return verdict;
}

} // namespace rawpage::fuzz

#endif // RAWPAGE_TESTS_FUZZ_ASSEMBLYFUZZBODY_H
