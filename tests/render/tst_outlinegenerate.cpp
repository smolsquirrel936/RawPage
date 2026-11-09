// tests/render/tst_outlinegenerate.cpp -- the generators graded against the labels, and a contents page whose links
// land. Stage 7 T11, guide A-79 and A-80 (`Stage7-Assembly-Testing-Guide.md` §5.13), decisions D7-10 and D7-11.
//
// THE ORACLES ARE THE CORPUS'S (D7-17, D7-18): `labels/assembly-tagged-headings.json` and its untagged twin name every
// heading -- its level, its text, the identity of the page it is on -- and the expected tree; a contents page's link is
// graded by the identity PDFium extracts from the page the link's destination names, never by an index our code
// computed. The pages are read as the window reads them: the engine's text and, for a tagged file, its structure tree.
//
// WHAT IT DOES NOT COVER (convention 19): the window's off-thread run of the same reading (`PageWorkRunner`) -- its
// queueing is T9's, measured there; and the contents page is graded by its TEXT and its links, not by a golden image
// (the plan's A-80 names one; recorded as a deviation in the guide): what it draws is D6-7's faces, which T7's golden
// cases already hold.

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/BookmarkCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/OutlineGenerator.h"
#include "core/doc/SaveCoordinator.h"
#include "core/doc/TableOfContents.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/PageOps.h"
#include "platform/win/WinDurableFiles.h"

#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
using core::OutlineItem;

namespace {

std::string identityOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page)
{
    core::ITextExtractor* extractor = backend.textExtractor(handle);
    REQUIRE(extractor != nullptr);
    PageTextModel model;
    REQUIRE(extractor->extractPage(handle, page, model));
    static const QRegularExpression pattern(QStringLiteral("(?<![A-Za-z0-9-])[A-Z][A-Z0-9]*-[0-9]{4}(?![A-Za-z0-9-])"));
    const QRegularExpressionMatch match = pattern.match(model.text());
    return match.hasMatch() ? match.captured(0).toStdString() : std::string("(none)");
}

// Every page's lines and structure headings, read as the window's run reads them.
struct Read {
    std::vector<core::HeadingLine> lines;
    std::vector<core::HeadingLine> structure;
};

Read readPages(Opened& opened)
{
    Read out;
    for (int page = 0; page < opened.pageCount(); ++page) {
        const PageTextModel model = extract(opened, page);
        for (core::HeadingLine& line : core::OutlineGenerator::linesOf(page, model)) out.lines.push_back(std::move(line));
        std::vector<core::HeadingLine> headings;
        REQUIRE(opened.backend.structureHeadings(opened.handle, page, headings));
        out.structure.insert(out.structure.end(), headings.begin(), headings.end());
    }
    return out;
}

// The label's expected tree as "  title @ identity" lines; the generated one the same way, each page by its identity.
std::vector<std::string> expected(const QJsonArray& items, int depth = 0)
{
    std::vector<std::string> out;
    for (const QJsonValue& value : items) {
        const QJsonObject item = value.toObject();
        out.push_back(std::string(static_cast<std::size_t>(depth) * 2, ' ') + item.value(QStringLiteral("text")).toString().toStdString()
                      + " @ " + item.value(QStringLiteral("pageIdentity")).toString().toStdString());
        for (std::string& child : expected(item.value(QStringLiteral("children")).toArray(), depth + 1)) out.push_back(std::move(child));
    }
    return out;
}

std::vector<std::string> generated(const std::vector<OutlineItem>& items, Opened& opened, int depth = 0)
{
    std::vector<std::string> out;
    for (const OutlineItem& item : items) {
        out.push_back(std::string(static_cast<std::size_t>(depth) * 2, ' ') + item.title.toStdString() + " @ "
                      + identityOf(opened.backend, opened.handle, item.destination.pageIndex));
        for (std::string& child : generated(item.children, opened, depth + 1)) out.push_back(std::move(child));
    }
    return out;
}

std::vector<std::string> labelledLines(const QJsonObject& label)
{
    std::vector<std::string> out;
    for (const QJsonValue& value : label.value(QStringLiteral("headings")).toArray())
        out.push_back(value.toObject().value(QStringLiteral("text")).toString().toStdString() + " @ "
                      + value.toObject().value(QStringLiteral("pageIdentity")).toString().toStdString());
    return out;
}

std::vector<std::string> flat(const std::vector<core::HeadingLine>& headings, Opened& opened)
{
    std::vector<std::string> out;
    for (const core::HeadingLine& line : headings)
        out.push_back(line.text.toStdString() + " @ " + identityOf(opened.backend, opened.handle, line.page));
    return out;
}

} // namespace

// ==========================================================================================
// A-79 -- the generators, graded against the labels
// ==========================================================================================

TEST_CASE("outline.generate: from headings -- the structure tree when tagged, the layout when not -- the labelled tree", "[outline][outlinegenerate][A-79]")
{
    for (const char* name : {"tagged-headings", "untagged-headings"}) {
        INFO(name);
        const QJsonObject label = loadLabel(std::string("assembly-") + name);
        auto opened = openCorpus(std::string("assembly/") + name + ".pdf");
        const Read read = readPages(*opened);
        const bool tagged = label.value(QStringLiteral("tagged")).toBool();
        // THE SOURCE IS THE RIGHT ONE: the tagged file's tree names its headings, the untagged file's names none.
        CHECK(read.structure.empty() != tagged);
        core::Result<std::vector<core::HeadingLine>> headings = read.structure;
        if (read.structure.empty()) headings = core::OutlineGenerator::select(read.lines, core::OutlineGeneratorOptions{});
        REQUIRE(headings);
        const std::vector<OutlineItem> tree = core::OutlineGenerator::tree(headings.value(), core::newBookmarkKey());
        CHECK(generated(tree, *opened) == expected(label.value(QStringLiteral("expectedOutline")).toArray()));
        // Each heading's level is the label's.
        const QJsonArray labelled = label.value(QStringLiteral("headings")).toArray();
        REQUIRE(headings.value().size() == static_cast<std::size_t>(labelled.size()));
        for (std::size_t i = 0; i < headings.value().size(); ++i)
            CHECK(headings.value()[i].level == labelled.at(static_cast<qsizetype>(i)).toObject().value(QStringLiteral("level")).toInt());
    }
}

TEST_CASE("outline.generate: by font size and style, and by a pattern -- exactly the labelled lines", "[outline][outlinegenerate][A-79]")
{
    const QJsonObject label = loadLabel("assembly-untagged-headings");
    auto opened = openCorpus("assembly/untagged-headings.pdf");
    const Read read = readPages(*opened);
    const std::vector<std::string> want = labelledLines(label);

    core::OutlineGeneratorOptions fonts;
    fonts.mode = core::OutlineGeneratorOptions::Mode::Fonts;
    fonts.minimumSize = label.value(QStringLiteral("headingSizes")).toObject().value(QStringLiteral("3")).toDouble();
    fonts.boldOnly = true;
    auto bySize = core::OutlineGenerator::select(read.lines, fonts);
    REQUIRE(bySize);
    CHECK(flat(bySize.value(), *opened) == want);
    // THE CONTROL: a threshold one point above the smallest heading drops exactly those headings.
    fonts.minimumSize += 1.0;
    auto fewer = core::OutlineGenerator::select(read.lines, fonts);
    REQUIRE(fewer);
    CHECK(fewer.value().size() == 6);

    core::OutlineGeneratorOptions pattern;
    pattern.mode = core::OutlineGeneratorOptions::Mode::Pattern;
    pattern.pattern = label.value(QStringLiteral("headingPattern")).toString();
    auto byPattern = core::OutlineGenerator::select(read.lines, pattern);
    REQUIRE(byPattern);
    CHECK(flat(byPattern.value(), *opened) == want);
    pattern.pattern = QStringLiteral("([");
    auto bad = core::OutlineGenerator::select(read.lines, pattern);
    REQUIRE_FALSE(bad);
    CHECK(QString(bad.error().messageKey) == QStringLiteral("error.outline.badPattern"));
}

TEST_CASE("outline.generate: text-a4.pdf -- the one larger line of each page, and nothing of its body", "[outline][outlinegenerate][A-79][control]")
{
    // THE CONTROL OVER UNLABELLED TEXT: three pages of 9 pt body copy under a 14 pt caption each (`corpus/make-ordinary.py`'s
    // `_label`). The generator must find those three captions and NOTHING of the body -- a threshold that let body lines
    // through would list hundreds.
    auto opened = openCorpus("ordinary/text-a4.pdf");
    const Read read = readPages(*opened);
    REQUIRE(read.lines.size() > 100);
    CHECK(read.structure.empty());
    auto headings = core::OutlineGenerator::select(read.lines, core::OutlineGeneratorOptions{});
    REQUIRE(headings);
    REQUIRE(headings.value().size() == 3);
    for (int page = 0; page < 3; ++page) {
        const core::HeadingLine& line = headings.value()[static_cast<std::size_t>(page)];
        CHECK(line.page == page);
        CHECK(line.level == 1);
        CHECK(line.text == QStringLiteral("RawPage corpus - text-a4.pdf - page %1 of 3").arg(page + 1));
    }
    // And a pattern no line matches finds nothing, by key.
    core::OutlineGeneratorOptions pattern;
    pattern.mode = core::OutlineGeneratorOptions::Mode::Pattern;
    pattern.pattern = QStringLiteral("^Chapter [0-9]+$");
    auto none = core::OutlineGenerator::select(read.lines, pattern);
    REQUIRE_FALSE(none);
    CHECK(QString(none.error().messageKey) == QStringLiteral("error.outline.noHeadings"));
}

// ==========================================================================================
// A-80 -- a table-of-contents page whose links land
// ==========================================================================================

TEST_CASE("outline.toc: one inserted page, one undo step, each link landing on its bookmark's page by identity", "[outline][outlinegenerate][A-80]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString path = scratch.filePath(QStringLiteral("document.pdf"));
    REQUIRE(QFile::copy(corpusPath("assembly/refs-every-kind.pdf"), path));
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    pdf::DocumentBackendImpl backend;
    platform::win::WinDurableFiles durableFiles;
    auto handle = backend.open(path, nullptr);
    REQUIRE(handle);
    core::Document document;
    REQUIRE(document.hydrate(backend, handle.value()));
    core::CommandJournal journal(std::move(document), backend.mutationSink(handle.value()));
    journal.setShowHandler([&](const core::Scope&) { return backend.showEdits(handle.value(), {}); },
                           [&] { return backend.settleEdits(handle.value()); });
    core::SaveCoordinator coordinator(backend, &durableFiles);
    const int pagesBefore = journal.document().pages().count();

    core::TableOfContents toc = core::tableOfContentsFrom(journal.document().outline(), journal.document().pages(), QStringLiteral("Contents"));
    REQUIRE(toc.entries.size() == 7);
    auto payload = backend.tableOfContents(toc);
    REQUIRE(payload);
    REQUIRE(journal.run(std::make_unique<core::InsertPagesCommand>(0, std::move(payload).value())));
    REQUIRE(journal.settle());
    CHECK(journal.document().pages().count() == pagesBefore + 1);
    // A TARGET MOVED AFTER THE INSERT: a link naming its page by index would now land on the wrong one (D7-2).
    REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{1}, pagesBefore)));   // P-0001 to the end
    REQUIRE(journal.settle());
    REQUIRE(coordinator.save(journal, path));

    pdf::DocumentBackendImpl fresh;
    auto reopened = fresh.open(path, nullptr);
    REQUIRE(reopened);
    // THE PAGE SHOWS ITS ENTRIES: the heading, each title, and each target's label.
    {
        core::ITextExtractor* extractor = fresh.textExtractor(reopened.value());
        PageTextModel model;
        REQUIRE(extractor->extractPage(reopened.value(), 0, model));
        const QString text = model.text();
        CHECK(text.contains(QStringLiteral("Contents")));
        for (const core::TableOfContentsEntry& entry : toc.entries) {
            INFO(entry.title.toStdString());
            CHECK(text.contains(entry.title));
        }
    }
    // EACH LINK, read from the saved file, lands where its title's identity shows.
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    auto opened = pdf::cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    REQUIRE(opened);
    auto walk = pdf::ops::PageOps::walk(opened.value().table);
    REQUIRE(walk);
    std::map<long long, int> indexOf;
    for (std::size_t i = 0; i < walk.value().pages.size(); ++i) indexOf.emplace(walk.value().pages[i].number, static_cast<int>(i));
    auto first = opened.value().table.fetch(walk.value().pages.front().number);
    REQUIRE(first);
    const pdf::cos::CosObject& annots = first.value().object.get("Annots");
    REQUIRE(annots.isArray());
    std::vector<std::string> landed;
    for (const pdf::cos::CosObject& reference : annots.items()) {
        auto link = opened.value().table.fetch(reference.reference().number);
        REQUIRE(link);
        const pdf::cos::CosObject& dest = link.value().object.get("Dest");
        REQUIRE(dest.isArray());
        REQUIRE(dest.items().front().isReference());
        const auto at = indexOf.find(dest.items().front().reference().number);
        REQUIRE(at != indexOf.end());
        landed.push_back(identityOf(fresh, reopened.value(), at->second));
    }
    std::vector<std::string> titles;
    for (const core::TableOfContentsEntry& entry : toc.entries) titles.push_back(entry.title.toStdString());
    CHECK(landed == titles);   // refs-every-kind's bookmarks are titled with the identity they go to (D7-17)
    std::cout << "A-80: " << landed.size() << " contents links landed by identity\n";

    // ONE UNDO STEP: the move and the insert undone, the document as it was.
    REQUIRE(journal.undo());
    REQUIRE(journal.undo());
    CHECK(journal.document().pages().count() == pagesBefore);

    // Q5(e): a title no face can show refuses the page, by its character.
    core::TableOfContents greek = toc;
    greek.entries.front().title = QStringLiteral("Ω chapter");
    auto refused = backend.tableOfContents(greek);
    REQUIRE_FALSE(refused);
    CHECK(QString(refused.error().messageKey) == QStringLiteral("error.edit.typedCharacterRefused"));
    // And a Traditional Chinese title is drawn, in a CJK face.
    core::TableOfContents han = toc;
    han.entries.front().title = QStringLiteral("第一章");
    han.hanPreference = core::TypedFont::Traditional;
    CHECK(backend.tableOfContents(han));
}
