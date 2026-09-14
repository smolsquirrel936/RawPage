// tst_pdfiumdoc -- guide cases A-42 and A-43. Stage 2 T9.
//
// WHAT THIS SUITE IS TRYING TO CATCH:
//
//   1. **Every corpus file opens**, with a page count that agrees with `corpus/PROVENANCE.md`. The
//      malformed set is allowed to open *repaired* -- but never to open silently wrong, which is
//      the half a "does it open" check misses.
//   2. **The page tree is real**, not a count. Boxes, rotation and labels are read, and the two
//      fixtures built to catch specific mistakes -- `rotated-pages.pdf` (identical content at
//      0/90/180/270) and `pages-count-wrong.pdf` (`/Count 97` over three real kids) -- are asserted
//      by name.
//   3. **The password callback** (A-43, D2-6): the right password opens, the wrong one is refused
//      by NAME, giving up is `Cancelled` rather than a failure, and the empty-user-password file
//      opens with no prompt at all.
//
// WHAT IT DOES NOT COVER, named at the assertion (CLAUDE.md convention 19):
//
//   * **Whether the pixels are right.** That is the golden suite's (A-48, T17). This asserts that a
//     document opens and describes itself correctly; a page that rendered upside down would pass
//     every case here.
//   * **PDFium's own repair.** PDFium silently reconstructs a broken file and reports nothing, so
//     "it opened" says nothing about how damaged it was. The `RepairReport` this suite checks comes
//     from the first-party COS reader run over the same bytes -- which is why `pdf/cos/XrefRepair`
//     is not redundant with an engine that already opens damaged files.

#include <catch2/catch_test_macros.hpp>

#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumInit.h"

#include <QFile>

#include <filesystem>
#include <string>

using rawpage::core::PasswordCallback;
using rawpage::pdf::render::PdfiumDocument;

namespace {

std::filesystem::path corpus(std::string_view relative)
{
    return std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative;
}

QString corpusPath(std::string_view relative)
{
    return QString::fromStdString(corpus(relative).string());
}

// A callback that must never run. Passing `nullptr` would also work and would prove less: this
// records whether it was ASKED, so a case can assert that an unencrypted file prompts for nothing.
struct Recorder {
    int asks = 0;
    std::optional<QString> reply;

    PasswordCallback callback()
    {
        return [this](int attempt) -> std::optional<QString> {
            ++asks;
            (void)attempt;
            return reply;
        };
    }
};

} // namespace

// ==========================================================================================
// A-42 -- every corpus file opens
// ==========================================================================================

TEST_CASE("pdfium opens every ordinary corpus file with a real page tree", "[render][document]")
{
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus("ordinary"))) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());

        Recorder recorder;
        auto document = PdfiumDocument::open(
            QString::fromStdString(entry.path().string()), recorder.callback());
        REQUIRE(document);

        // A page COUNT is not a page tree. Every page must have a usable display size, or the
        // count is a number with nothing behind it -- which is what a reader that trusted
        // `/Pages /Count` would produce.
        const int count = document.value()->pageCount();
        REQUIRE(count > 0);
        for (int index = 0; index < count; ++index) {
            const auto* page = document.value()->pages().at(index);
            REQUIRE(page != nullptr);
            REQUIRE(page->index() == index);
            CHECK(page->displaySize().width > 0.0);
            CHECK(page->displaySize().height > 0.0);
            // Every box resolves inside the MediaBox, which is the rule `core/doc/Page` enforces
            // and real files violate.
            CHECK(page->cropBox().size.width <= page->mediaBox().size.width + 0.001);
            CHECK(page->cropBox().size.height <= page->mediaBox().size.height + 0.001);
        }

        // An undamaged file asks for no password. Without this, the suite could not tell a
        // correctly-opened file from one that prompted and got lucky.
        CHECK(recorder.asks == 0);
        ++files;
    }
    CHECK(files >= 25);
}

TEST_CASE("pdfium reads the page count from the tree rather than from /Count", "[render][document]")
{
    // `corpus/malformed/pages-count-wrong.pdf`: `/Pages` declares `/Count 97` over three real kids.
    // PROVENANCE says the answer has to be the TREE -- the count goes in the status bar before the
    // tree has been walked, and a reader that trusted it shows "page 1 of 97" for a 3-page file.
    Recorder recorder;
    auto document = PdfiumDocument::open(corpusPath("malformed/pages-count-wrong.pdf"),
                                         recorder.callback());
    REQUIRE(document);
    CHECK(document.value()->pageCount() == 3);
    CHECK(document.value()->pageCount() != 97);
}

TEST_CASE("pdfium reads rotation as degrees rather than quarter turns", "[render][document]")
{
    // `corpus/ordinary/rotated-pages.pdf`: `/Rotate` 0, 90, 180, 270 over IDENTICAL content, so the
    // only difference between the four pages is the rotation.
    //
    // PDFium reports rotation in QUARTER TURNS (0..3), not degrees. Passing that straight through
    // turns every 270-degree page into 3 degrees, which normalizes to 0 -- and a landscape page
    // then displays sideways with no error anywhere.
    Recorder recorder;
    auto document = PdfiumDocument::open(corpusPath("ordinary/rotated-pages.pdf"),
                                         recorder.callback());
    REQUIRE(document);
    REQUIRE(document.value()->pageCount() == 4);

    const auto& pages = document.value()->pages();
    CHECK(pages.rotation(0) == rawpage::core::PageRotation::None);
    CHECK(pages.rotation(1) == rawpage::core::PageRotation::Clockwise90);
    CHECK(pages.rotation(2) == rawpage::core::PageRotation::Half);
    CHECK(pages.rotation(3) == rawpage::core::PageRotation::Clockwise270);

    // And the quarter-turn pages report a TRANSPOSED display size, which is the consequence that
    // matters to layout: identical content, and pages 1 and 3 are landscape.
    const auto upright = pages.displaySize(0);
    const auto turned = pages.displaySize(1);
    CHECK(turned.width == upright.height);
    CHECK(turned.height == upright.width);
}

TEST_CASE("pdfium resolves a rotation inherited from an ancestor Pages node", "[render][document]")
{
    // `corpus/ordinary/rotate-inherited.pdf` (Stage 3 T1, guide case A-17 of
    // `Stage3-Mutation-Testing-Guide.md`): `/Rotate 90` on the ROOT `/Pages` node only. Page 1
    // inherits it, page 2 overrides it with an explicit `/Rotate 0`, and page 3 inherits it through
    // an intermediate `/Pages` node that has no `/Rotate` of its own.
    //
    // It asserts the fixture carries its fault as the ENGINE reads it -- which is the reading the
    // user sees, and the one Stage 3's writer (D3-5) and pre-flight check (D3-7) will be compared
    // against. What it does NOT cover: how the first-party COS reader resolves inheritance; that is
    // `pdf/ops/PageOps`' page-object walk, Stage 3 T3.
    Recorder recorder;
    auto document = PdfiumDocument::open(corpusPath("ordinary/rotate-inherited.pdf"),
                                         recorder.callback());
    REQUIRE(document);
    REQUIRE(document.value()->pageCount() == 3);

    const auto& pages = document.value()->pages();
    CHECK(pages.rotation(0) == rawpage::core::PageRotation::Clockwise90);
    CHECK(pages.rotation(1) == rawpage::core::PageRotation::None);
    CHECK(pages.rotation(2) == rawpage::core::PageRotation::Clockwise90);

    // The CONTROL for the three above: page 2's explicit zero is what separates "inherited" from
    // "every page reads 90 whatever it says", and its display size is the untransposed A4 that the
    // two inheriting pages are the transpose of.
    const auto overridden = pages.displaySize(1);
    CHECK(overridden.height > overridden.width);
    for (const int inheriting : {0, 2}) {
        INFO("page index " << inheriting);
        const auto turned = pages.displaySize(inheriting);
        CHECK(turned.width == overridden.height);
        CHECK(turned.height == overridden.width);
    }
    CHECK(recorder.asks == 0);
}

TEST_CASE("pdfium opens a 900-page document and indexes it directly", "[render][document]")
{
    // `corpus/ordinary/pages-900.pdf`: 900 pages in a balanced 30x30 tree with object streams.
    // PROVENANCE says a flat `/Kids` array is exactly the shape that hides an O(n) page lookup
    // behind a fast small document -- so the last page is asked for by index, not walked to.
    Recorder recorder;
    auto document = PdfiumDocument::open(corpusPath("ordinary/pages-900.pdf"),
                                         recorder.callback());
    REQUIRE(document);
    CHECK(document.value()->pageCount() == 900);
    REQUIRE(document.value()->pages().at(899) != nullptr);
    CHECK(document.value()->pages().at(899)->index() == 899);
    CHECK(document.value()->pages().at(900) == nullptr);
}

TEST_CASE("pdfium opens every malformed corpus file or refuses by name", "[render][document]")
{
    // The malformed set is allowed to open REPAIRED -- but never to open silently wrong. PDFium
    // does its own repair and says nothing about it, so the report comes from the first-party COS
    // reader over the same bytes, and that is the only way a user learns the file was damaged.
    std::size_t files = 0;
    std::size_t opened = 0;
    std::size_t reportedRepairs = 0;

    for (const auto& entry : std::filesystem::directory_iterator(corpus("malformed"))) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        ++files;

        Recorder recorder;
        auto document = PdfiumDocument::open(
            QString::fromStdString(entry.path().string()), recorder.callback());

        if (!document) {
            // A NAMED refusal, never a bare one.
            CHECK(document.error().messageKey.size() > 0);
            continue;
        }
        ++opened;
        // A DOCUMENT THAT OPENS HAS A PAGE. This asserted `>= 0` until I-31, which every document
        // satisfies -- including the zero-page one the product then showed as an empty window.
        CHECK(document.value()->pageCount() > 0);
        if (document.value()->wasRepaired())
            ++reportedRepairs;
    }

    CHECK(files >= 20);
    // Both branches must occur, or the case is exercising one of them.
    INFO("opened " << opened << " of " << files << ", " << reportedRepairs << " reported repairs");
    CHECK(opened > 0);

    // AND THE POINT OF THE CASE: at least one file that PDFium opened happily was reported as
    // damaged by our own reader. Without this, "it opened" is all anyone would ever learn, and
    // Stage 3's forced Save-As would have nothing to trigger on.
    CHECK(reportedRepairs > 0);
}

TEST_CASE("pdfium refuses bytes that are not a PDF by name", "[render][document]")
{
    Recorder recorder;

    auto missing = PdfiumDocument::open(corpusPath("no-such-file-anywhere.pdf"),
                                        recorder.callback());
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == rawpage::core::ErrorCode::NotFound);
    CHECK(missing.error().messageKey == QLatin1String("error.open.notFound"));

    auto empty = PdfiumDocument::openBytes(QByteArray(), QStringLiteral("empty.pdf"),
                                           recorder.callback());
    REQUIRE_FALSE(empty);
    CHECK(empty.error().messageKey == QLatin1String("error.open.notPdf"));

    auto junk = PdfiumDocument::openBytes(QByteArray("this is not a PDF at all, not even slightly"),
                                          QStringLiteral("junk.pdf"), recorder.callback());
    REQUIRE_FALSE(junk);
    CHECK(junk.error().messageKey == QLatin1String("error.open.notPdf"));

    // None of those is an encryption problem, so nothing should have been asked for a password.
    CHECK(recorder.asks == 0);
}

TEST_CASE("a damaged PDF the engine cannot open is refused as damaged and not as not-a-PDF", "[render][document]")
{
    // STAGE 2 ISSUE I-32. PDFium reports `FPDF_ERR_FORMAT` for a spreadsheet and for a PDF whose
    // trailer is gone alike, and the product used to say "is not a PDF file" for both. The four
    // corpus files PDFium refuses all carry a `%PDF-` header and are damaged PDFs by construction
    // (`corpus/PROVENANCE.md`).
    for (const char* name : {"missing-endobj.pdf", "no-root.pdf", "root-points-at-nothing.pdf",
                             "truncated-mid-stream.pdf"}) {
        INFO("file: " << name);
        Recorder recorder;
        auto refused = PdfiumDocument::open(corpusPath(std::string("malformed/") + name),
                                            recorder.callback());
        REQUIRE_FALSE(refused);
        CHECK(refused.error().messageKey == QLatin1String("error.open.damaged"));
        CHECK(refused.error().context == QLatin1String(name));
    }

    // THE CONTROL (convention 7): the same kind of failure with no header is still "not a PDF", so
    // the key above is not simply what every refusal now says. `junk-before-header.pdf` is the
    // other side -- 1,024 bytes of junk and then a header -- and it OPENS, which the corpus loop
    // above asserts.
    Recorder recorder;
    auto junk = PdfiumDocument::openBytes(QByteArray("Dear diary, today I was not a PDF.\n"),
                                          QStringLiteral("diary.pdf"), recorder.callback());
    REQUIRE_FALSE(junk);
    CHECK(junk.error().messageKey == QLatin1String("error.open.notPdf"));
}

TEST_CASE("a document with no readable page is refused rather than opened empty", "[render][document]")
{
    // STAGE 2 ISSUE I-31, the silent empty window M-07 forbids. PDFium opens
    // `corpus/malformed/objstm-corrupt.pdf` -- its classic xref is fine -- and every object in the
    // corrupt object stream is unreadable, so the page tree has no page. It used to open as a tab
    // showing nothing, with no banner, because our own reader's repair report covers the xref and
    // found nothing wrong there.
    Recorder recorder;
    auto document = PdfiumDocument::open(corpusPath("malformed/objstm-corrupt.pdf"),
                                         recorder.callback());
    REQUIRE_FALSE(document);
    CHECK(document.error().messageKey == QLatin1String("error.open.noPages"));
    CHECK(document.error().context == QLatin1String("objstm-corrupt.pdf"));
    CHECK(recorder.asks == 0);
}

// ==========================================================================================
// A-43 -- the password callback
// ==========================================================================================

TEST_CASE("pdfium opens an encrypted document with the right password", "[render][document]")
{
    // GUIDE CASE A-43, and decision D2-6: PDFium opens an encrypted document through the password
    // callback while `pdf/cos/` declines it by name until Stage 10. Two readers, two different
    // correct answers -- an arrangement rather than an inconsistency.
    //
    // The passwords are PUBLIC and recorded in `corpus/PROVENANCE.md` (`user` / `owner`), because a
    // fixture whose password is a secret is a fixture nobody can use.
    for (const char* name : {"encrypted/rc4-40.pdf", "encrypted/rc4-128.pdf",
                             "encrypted/aes-128.pdf", "encrypted/aes-256.pdf"}) {
        INFO("file: " << name);
        Recorder recorder;
        recorder.reply = QStringLiteral("user");

        auto document = PdfiumDocument::open(corpusPath(name), recorder.callback());
        REQUIRE(document);
        CHECK(document.value()->pageCount() > 0);

        // It was ASKED, which is what distinguishes an encrypted file that opened from an
        // unencrypted one. Without this the case would pass against a corpus of plain documents.
        CHECK(recorder.asks >= 1);
    }
}

TEST_CASE("pdfium refuses an encrypted document given the wrong password", "[render][document]")
{
    Recorder recorder;
    recorder.reply = QStringLiteral("definitely-not-the-password");

    auto document = PdfiumDocument::open(corpusPath("encrypted/aes-256.pdf"), recorder.callback());
    REQUIRE_FALSE(document);

    // `PermissionDenied` and a NAMED key -- not `ParseFailure`. The file is fine and we have no
    // key; saying the document is damaged would send the reader after the wrong problem.
    CHECK(document.error().code == rawpage::core::ErrorCode::PermissionDenied);
    CHECK(document.error().messageKey == QLatin1String("error.open.badPassword"));

    // And it stopped asking rather than spinning: a callback that always returns the same wrong
    // string is exactly what a scripted caller does.
    CHECK(recorder.asks > 1);
    CHECK(recorder.asks <= 16);
}

TEST_CASE("pdfium reports a cancelled password prompt as cancelled", "[render][document]")
{
    // Giving up is NOT a failure to open. A cancelled prompt is not a damaged file, and a message
    // bar that said so would be lying to the person who just pressed Escape.
    Recorder recorder;
    recorder.reply = std::nullopt;

    auto document = PdfiumDocument::open(corpusPath("encrypted/aes-128.pdf"), recorder.callback());
    REQUIRE_FALSE(document);
    CHECK(document.error().code == rawpage::core::ErrorCode::Cancelled);
    CHECK(document.error().messageKey == QLatin1String("error.open.cancelled"));
    CHECK(recorder.asks == 1);
}

TEST_CASE("pdfium opens an empty-user-password file with no prompt", "[render][document]")
{
    // `corpus/encrypted/owner-empty-user.pdf` is the one to know about: an EMPTY user password, so
    // it opens with no prompt while still being encrypted. PROVENANCE calls it *the case a reader
    // mistakes for an unencrypted file* -- and the mistake this asserts against is the opposite
    // one, a reader that prompts for a password nobody has to type.
    Recorder recorder;
    recorder.reply = QStringLiteral("wrong-on-purpose");

    auto document = PdfiumDocument::open(corpusPath("encrypted/owner-empty-user.pdf"),
                                         recorder.callback());
    REQUIRE(document);
    CHECK(document.value()->pageCount() > 0);
    CHECK(recorder.asks == 0);
}

TEST_CASE("pdfium refuses an encrypted document when there is nobody to ask", "[render][document]")
{
    // A batch run and a headless test both land here. It is a REFUSAL rather than a parse failure:
    // the file is fine and we have no key and no user.
    auto document = PdfiumDocument::open(corpusPath("encrypted/rc4-128.pdf"), nullptr);
    REQUIRE_FALSE(document);
    CHECK(document.error().code == rawpage::core::ErrorCode::PermissionDenied);
    CHECK(document.error().messageKey == QLatin1String("error.open.badPassword"));
}

TEST_CASE("pdfium is initialized exactly once however many documents open", "[render][document]")
{
    // A-45's first half, restated where documents are opened rather than only where the library is.
    // `PdfiumInit` is the one place PDFium's global state is touched (D2-9), and `initializationCount`
    // exists so "entered once" is a fact a test asserts rather than a property nobody measures.
    Recorder recorder;
    for (const char* name : {"minimal.pdf", "ordinary/text-a4.pdf", "ordinary/text-letter.pdf"}) {
        auto document = PdfiumDocument::open(corpusPath(name), recorder.callback());
        REQUIRE(document);
    }
    CHECK(rawpage::pdf::render::initializationCount() == 1);
    CHECK(rawpage::pdf::render::isInitialized());
}
