// tst_docmodels -- the READER half of Stage 4 T4: what `pdf/render/PdfiumDocument` actually gets
// out of a real file for `core/doc/{Outline,Destinations,Metadata}`. Guide cases A-25, A-26, A-27,
// A-28 and A-30 (`Stage4-ViewingSurface-Testing-Guide.md` §5.6); decisions D4-3 and D4-4.
//
// WHY IT IS HERE AND NOT IN `tests/unit`. The models are `core/` types and their arithmetic is
// tested there (`tst_outline.cpp`, `tst_metadata.cpp`) with no engine linked, because
// `rawpage_assert_headless()` fails configure if that target ever links one. Every claim in THIS
// file is about a reader meeting a file, so it belongs in the suite that links the engine. Both
// halves are needed and neither is sufficient: one proves the shape is computed correctly over data
// nobody read, the other proves the data is what a file actually says.
//
// WHAT IT DOES NOT COVER, at the assertion (convention 19): nothing here says a pane DISPLAYS any
// of it. `BookmarksPane` and `DestinationsPane` are T6's, and Document Properties is T12's.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Destinations.h"
#include "core/doc/Metadata.h"
#include "core/doc/Outline.h"
#include "core/doc/PageTransitions.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/render/PdfiumDocument.h"

#include <QByteArray>

#include <filesystem>
#include <string>
#include <string_view>

using rawpage::core::Destination;
using rawpage::core::NamedDestination;
using rawpage::core::OutlineItem;
using rawpage::pdf::DocumentBackendImpl;
using rawpage::pdf::render::PdfiumDocument;

namespace {

// Catch2 cannot stringify a `QString`: a failed comparison prints `{ {?}, {?}, ... }` on both
// sides, which says nothing about what was actually read. Comparing the `std::string` makes a
// wrong expectation legible at the point of failure -- the same lesson as Stage 3's I-09, where
// two `QByteArray`s crashed the reporter outright, applied one type over.
std::string text(const QString& value)
{
    return value.toStdString();
}

QString corpusPath(std::string_view relative)
{
    return QString::fromStdString((std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative).string());
}

std::unique_ptr<PdfiumDocument> openCorpus(std::string_view relative)
{
    auto opened = PdfiumDocument::open(corpusPath(relative), {});
    REQUIRE(opened);
    return std::move(opened).value();
}

const OutlineItem* childNamed(const std::vector<OutlineItem>& items, const QString& title)
{
    for (const OutlineItem& item : items)
        if (item.title == title) return &item;
    return nullptr;
}

} // namespace

TEST_CASE("the outline is read with its structure and its open state",
          "[docmodels]")
{
    // `corpus/ordinary/outline-labels.pdf` carries a two-level `/Outlines` tree: two chapters, the
    // first with one nested section. `PROVENANCE.md` is the record of what it holds.
    const auto document = openCorpus("ordinary/outline-labels.pdf");
    const rawpage::core::Outline& outline = document->outline();

    REQUIRE_FALSE(outline.isEmpty());
    REQUIRE(outline.roots().size() == 2);
    REQUIRE(outline.nodeCount() == 3);
    REQUIRE(outline.depth() == 2);

    const OutlineItem* chapterOne =
        childNamed(outline.roots(), QStringLiteral("1 front matter and body"));
    REQUIRE(chapterOne != nullptr);
    const OutlineItem* chapterTwo = childNamed(outline.roots(), QStringLiteral("2 the end"));
    REQUIRE(chapterTwo != nullptr);

    // THE PARENT/CHILD RELATIONSHIP, not just the count: an outline read as a flat list of three
    // would satisfy `nodeCount()` and fail a user.
    REQUIRE(chapterOne->children.size() == 1);
    REQUIRE(chapterTwo->children.empty());
    REQUIRE(text(chapterOne->children.front().title) == "1.1 a nested section");

    // The destinations resolve to page INDICES -- the file points chapter one at index 3, its
    // section at index 4 and chapter two at index 7.
    REQUIRE(chapterOne->destination.isResolved());
    REQUIRE(chapterOne->destination.pageIndex == 3);
    REQUIRE(chapterOne->destination.fit == Destination::Fit::Fit);
    REQUIRE(chapterTwo->destination.pageIndex == 7);

    const OutlineItem& section = chapterOne->children.front();
    REQUIRE(section.destination.pageIndex == 4);
    REQUIRE(section.destination.fit == Destination::Fit::XYZ);
    // `/XYZ null 780 null`: the file gives a top and leaves x and the zoom null, and *null* is not
    // *zero* -- a reader that conflated them would scroll to the page's bottom-left corner.
    REQUIRE(section.destination.hasY);
    REQUIRE(section.destination.y == 780.0);
    REQUIRE_FALSE(section.destination.hasX);
    REQUIRE_FALSE(section.destination.hasZoom);

    // The open/closed flag is what the FILE asks for at first display (ISO 32000-1 Table 153).
    // outline-labels.pdf writes `/Count 1` on chapter one -- positive, so open.
    REQUIRE(chapterOne->openByDefault);
    REQUIRE_FALSE(chapterTwo->openByDefault);   // no children, so no request either way
}

TEST_CASE("destinations of three kinds resolve and the dangling one is reported", "[docmodels]")
{
    // `corpus/ordinary/dests-named.pdf` (Stage 4 T1) carries BOTH forms -- a two-level
    // `/Names /Dests` tree and the PDF 1.1 flat `/Dests` dictionary -- three destination kinds, and
    // one destination naming object 9999, which does not exist.
    const auto document = openCorpus("ordinary/dests-named.pdf");
    const rawpage::core::Destinations& destinations = document->destinations();

    REQUIRE_FALSE(destinations.isEmpty());

    // /XYZ with a point, from the name tree's dictionary form.
    const NamedDestination* chapterOne = destinations.find(QStringLiteral("chapter-one"));
    REQUIRE(chapterOne != nullptr);
    REQUIRE(chapterOne->destination.pageIndex == 0);
    REQUIRE(chapterOne->destination.fit == Destination::Fit::XYZ);
    REQUIRE(chapterOne->destination.hasX);
    REQUIRE(chapterOne->destination.hasY);
    REQUIRE(chapterOne->destination.x == 72.0);
    REQUIRE(chapterOne->destination.y == 720.0);

    // /Fit -- a page and nothing else -- from the bare-array form.
    const NamedDestination* chapterTwo = destinations.find(QStringLiteral("chapter-two"));
    REQUIRE(chapterTwo != nullptr);
    REQUIRE(chapterTwo->destination.pageIndex == 1);
    REQUIRE(chapterTwo->destination.fit == Destination::Fit::Fit);

    // /FitH -- a page and a top.
    const NamedDestination* figures = destinations.find(QStringLiteral("table-of-figures"));
    REQUIRE(figures != nullptr);
    REQUIRE(figures->destination.pageIndex == 2);
    REQUIRE(figures->destination.fit == Destination::Fit::FitH);
    REQUIRE(figures->destination.hasY);
    REQUIRE(figures->destination.y == 640.0);

    // **THE CONTROL.** `ghost-page` names an object that is not there. It must be present in the
    // table and unresolved -- reported rather than silently dropped (D4-3) -- and the reader must
    // not have stopped at it, which the four assertions above already prove since two of them read
    // entries the walk reaches after it.
    const NamedDestination* ghost = destinations.find(QStringLiteral("ghost-page"));
    REQUIRE(ghost != nullptr);
    REQUIRE_FALSE(ghost->destination.isResolved());
    REQUIRE(ghost->destination.pageIndex == -1);
    REQUIRE(destinations.unresolvedCount() == 1);

    // WHAT THIS DOES NOT PROVE, said here rather than left to look like coverage: which of the two
    // PDF tables each name came from. PDFium merges them into one enumeration and the facade keeps
    // that merge (`core/doc/Destinations.h` says why), so a file carrying only the name tree would
    // satisfy every assertion above. That the file carries both forms is asserted over its BYTES,
    // by the corpus check and `PROVENANCE.md`, not here.
}

TEST_CASE("a document with no outline and no destinations answers empty", "[docmodels]")
{
    // Guide case A-27. Empty is the ORDINARY answer, and it is not a failure: the pane shows its
    // empty state rather than an error.
    const auto document = openCorpus("ordinary/text-a4.pdf");
    REQUIRE(document->outline().isEmpty());
    REQUIRE(document->outline().nodeCount() == 0);
    REQUIRE(document->destinations().isEmpty());
    REQUIRE(document->destinations().unresolvedCount() == 0);
}

TEST_CASE("the Info dictionary is read and XMP is not claimed", "[docmodels]")
{
    const auto document = openCorpus("ordinary/text-a4.pdf");
    const rawpage::core::Metadata& metadata = document->metadata();

    REQUIRE_FALSE(metadata.isEmpty());
    REQUIRE(text(metadata.title) == "RawPage corpus: plain A4 text");
    REQUIRE(text(metadata.producer) == "RawPage corpus generator");
    // The corpus writes one fixed instant (`pdfbuild.FIXED_DATE`), so the date is knowable rather
    // than merely non-empty -- and it is carried RAW, exactly as stored.
    REQUIRE(text(metadata.creationDate) == "D:20260831000000Z");
    REQUIRE(text(metadata.modificationDate) == "D:20260831000000Z");
    REQUIRE(rawpage::core::Metadata::parseDate(metadata.creationDate).has_value());

    // From the file HEADER, not from `/Info`.
    REQUIRE(text(metadata.pdfVersion) == "1.7");

    // **NO XMP IS READ (D4-4).** This case states it rather than the absence being inferred from a
    // field nobody looked at: XMP, its round-tripping, and any policy for reconciling it with
    // `/Info` are Stage 8's, with the writer that has to keep the two consistent. There is no API
    // on `core::Metadata` that could answer an XMP question, which is the mechanical form of the
    // same claim.
}

TEST_CASE("a document with no Info dictionary answers empty rather than failing", "[docmodels]")
{
    // `corpus/minimal.pdf` is Stage 0's seed and carries no `/Info` at all -- so this is the
    // *"one without"* half of A-28, on a file that has been in the corpus since the first week.
    const auto document = openCorpus("minimal.pdf");
    const rawpage::core::Metadata& metadata = document->metadata();

    REQUIRE(metadata.isEmpty());
    REQUIRE(metadata.title.isEmpty());
    REQUIRE(metadata.producer.isEmpty());
    REQUIRE(metadata.creationDate.isEmpty());
    // The version still reads: it is in the header, which every PDF has.
    REQUIRE(text(metadata.pdfVersion) == "1.7");
}

TEST_CASE("a malformed date survives the reader exactly as the file wrote it", "[docmodels]")
{
    // Guide case A-28's third input. No corpus file carries a malformed date -- the generators
    // write one fixed instant and T1's corpus is closed -- so the document is built HERE, in
    // bytes, and opened through the facade's `openBytes`. The claim is about the READER: it must
    // hand the string over untouched, so a properties dialog can show what the file says instead of
    // an invented substitute or an empty field (D4-4).
    const QByteArray pdf =
        "%PDF-1.4\n"
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>\nendobj\n"
        "4 0 obj\n<< /Title (a document) /CreationDate (Tuesday afternoon) >>\nendobj\n"
        "trailer\n<< /Size 5 /Root 1 0 R /Info 4 0 R >>\n";

    auto opened = PdfiumDocument::openBytes(pdf, QStringLiteral("malformed-date.pdf"), {});
    REQUIRE(opened);
    const std::unique_ptr<PdfiumDocument> document = std::move(opened).value();
    const rawpage::core::Metadata& metadata = document->metadata();

    REQUIRE(text(metadata.title) == "a document");
    REQUIRE(text(metadata.creationDate) == "Tuesday afternoon");
    REQUIRE_FALSE(rawpage::core::Metadata::parseDate(metadata.creationDate).has_value());

    // This file has no cross-reference table, so PDFium reconstructs it silently -- which is
    // exactly the behaviour `PdfiumDocument` documents and is not what this case is about. It is
    // noted so the next reader does not take the open as evidence of anything else.
}

// =============================================================================================
// A-61's READER half (S4 T10) -- `/Trans`, `/Dur` and `/PageMode` off a real file
// =============================================================================================
// **THE ONE READ IN THIS PRODUCT THAT PDFIUM CANNOT DO.** D4-3 settled that the outline, the
// destinations and the Info dictionary are read THROUGH the engine; the engine's C API exposes
// page transitions nowhere at all, so `pdf/ops/TransitionReader` reads them first-party off the
// page walk `DocumentBackendImpl::open` already runs on every document. This case is what says
// the exception actually works against a file rather than against a fake.
//
// It goes through the BACKEND rather than `PdfiumDocument`, unlike every case above, because the
// walk it reads from is the backend's and not the engine's -- which is the whole point.
TEST_CASE("the per-page transitions are read first-party, and PDFium never sees them",
          "[docmodels][transitions]")
{
    DocumentBackendImpl backend;
    auto opened = backend.open(corpusPath("ordinary/transitions.pdf"), nullptr);
    REQUIRE(opened);

    const rawpage::core::PageTransitions* transitions = backend.pageTransitions(opened.value());
    REQUIRE(transitions != nullptr);
    REQUIRE(transitions->pageCount() == 4);
    CHECK_FALSE(transitions->isEmpty());

    // `/PageMode /FullScreen` in the catalog: the document asks to open as a presentation. READ
    // and reported; acting on it is deliberately not Stage 4's -- opening someone's file
    // full-screen because it said so needs a preference to refuse it, and no declared key holds one.
    CHECK(transitions->opensFullScreen());

    // Page 1 -- `/Trans << /S /Wipe /D 1 /Di 0 >>`.
    const rawpage::core::PageTransition* wipe = transitions->at(0);
    REQUIRE(wipe != nullptr);
    CHECK(wipe->style == rawpage::core::TransitionStyle::Wipe);
    CHECK(wipe->supported);
    CHECK(text(wipe->rawStyle) == "Wipe");
    REQUIRE(wipe->durationSeconds.has_value());
    CHECK(*wipe->durationSeconds == 1.0);
    CHECK(text(wipe->direction) == "0");

    // Page 2 -- `/Trans << /S /Dissolve /D 0.5 >>`, and no `/Dur`.
    const rawpage::core::PageTransition* dissolve = transitions->at(1);
    REQUIRE(dissolve != nullptr);
    CHECK(dissolve->style == rawpage::core::TransitionStyle::Dissolve);
    REQUIRE(dissolve->durationSeconds.has_value());
    CHECK(*dissolve->durationSeconds == 0.5);
    CHECK(transitions->timingAt(1) == nullptr);

    // Page 3 -- `/Trans << /S /Split /D 1.5 /Dm /V /M /O >>`. **`/Split` IS NOT ONE OF THE FOUR
    // THIS PRODUCT DRAWS**, and the model says so rather than approximating it: the style reads
    // `None` -- a cut -- and the file's own word is kept beside it, so a page we cannot animate
    // stays distinguishable from one that asked for a cut. Approximating it with a wipe would be
    // presenting an animation the author did not ask for.
    const rawpage::core::PageTransition* split = transitions->at(2);
    REQUIRE(split != nullptr);
    CHECK(split->style == rawpage::core::TransitionStyle::None);
    CHECK_FALSE(split->supported);
    CHECK(text(split->rawStyle) == "Split");
    CHECK(text(split->dimension) == "V");
    CHECK(text(split->motion) == "O");
    CHECK(transitions->unsupportedCount() == 1);

    // **PAGE 4 CARRIES NEITHER KEY, AND IT IS THE CONTROL** the override needs: absent is not
    // `None`. A model that stored `None` for a page with no `/Trans` would make
    // `FullScreenController::transitionFor` unable to tell "the author asked for a cut" from "the
    // author said nothing", and the per-page override would silently stop working.
    CHECK(transitions->at(3) == nullptr);
    CHECK(transitions->timingAt(3) == nullptr);

    // `/Dur 3` and `/Dur 5` on pages 1 and 3 -- a key on the PAGE, not inside `/Trans`: how long
    // the page is SHOWN rather than how long the animation takes. The two are one letter apart in
    // ISO 32000-1 and confusing them makes a one-second fade into a one-second slide.
    REQUIRE(transitions->timingAt(0) != nullptr);
    CHECK(transitions->timingAt(0)->seconds == 3.0);
    REQUIRE(transitions->timingAt(2) != nullptr);
    CHECK(transitions->timingAt(2)->seconds == 5.0);

    CHECK(backend.close(opened.value()));
}

TEST_CASE("a document with no presentation instructions reads empty, which is the ordinary answer",
          "[docmodels][transitions]")
{
    // THE CONTROL FOR THE WHOLE READER. Almost no PDF carries a `/Trans`, so "empty" has to be the
    // answer for an ordinary file rather than a failure -- and `isEmpty()` is what lets the
    // controller skip the model entirely for the overwhelming majority of documents.
    DocumentBackendImpl backend;
    auto opened = backend.open(corpusPath("ordinary/text-a4.pdf"), nullptr);
    REQUIRE(opened);

    const rawpage::core::PageTransitions* transitions = backend.pageTransitions(opened.value());
    REQUIRE(transitions != nullptr);
    CHECK(transitions->isEmpty());
    CHECK_FALSE(transitions->opensFullScreen());
    CHECK(transitions->unsupportedCount() == 0);
    CHECK(transitions->at(0) == nullptr);

    CHECK(backend.close(opened.value()));
}
