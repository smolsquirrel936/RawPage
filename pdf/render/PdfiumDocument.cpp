#include "pdf/render/PdfiumDocument.h"

#include "core/Log.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/render/PdfiumInit.h"

// fpdfview.h alone is not enough, and the split is not where you would guess: the page BOXES are
// in fpdf_transformpage.h (they share a header with the functions that MOVE a page, not with the
// ones that describe it), the ROTATION is in fpdf_edit.h, and the page LABEL in fpdf_doc.h.
#include <fpdf_doc.h>
#include <fpdf_edit.h>
#include <fpdf_formfill.h>
#include <fpdf_transformpage.h>
#include <fpdfview.h>

#include <QFile>
#include <QFileInfo>

#include <mutex>
#include <vector>

namespace rawpage::pdf::render {
namespace {

core::Error openError(core::ErrorCode code, const char* key, const QString& context)
{
    return core::Error{code, QLatin1String(key), context};
}

// DOES THIS LOOK LIKE A PDF AT ALL? Asked only once PDFium has already refused the bytes, and only to
// choose between two refusals that PDFium reports identically (`FPDF_ERR_FORMAT` for both): *the file
// is damaged* and *this is not a PDF*. Stage 2 issue I-32 -- every damaged corpus file PDFium could
// not open was reported as "not a PDF file", which is false and sent the reader looking for the wrong
// problem.
//
// A `%PDF-` header anywhere in the first 4 KiB. ISO 32000 lets a header sit behind up to 1,024 bytes
// of junk (`corpus/malformed/junk-before-header.pdf` puts it exactly there), and the window is wider
// than that on purpose: this only picks a SENTENCE, so a generous window costs nothing, while a tight
// one would call a damaged file with a longer preamble "not a PDF" again.
bool looksLikePdf(const QByteArray& bytes)
{
    constexpr qsizetype kHeaderWindow = 4096;
    return bytes.left(kHeaderWindow).contains("%PDF-");
}

// A page label, as UTF-16LE from `FPDF_GetPageLabel`. Empty when the document has none, which is
// the ordinary case -- most files number their pages implicitly.
QString pageLabel(FPDF_DOCUMENT document, int index)
{
    const unsigned long bytes = FPDF_GetPageLabel(document, index, nullptr, 0);
    // 2 is the terminator alone. PDFium reports the size INCLUDING it, so a label of zero
    // characters and no label at all are indistinguishable here -- and both mean "no label".
    if (bytes <= 2)
        return {};

    std::vector<char> buffer(bytes);
    const unsigned long written = FPDF_GetPageLabel(document, index, buffer.data(), bytes);
    if (written <= 2)
        return {};
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()),
                              static_cast<qsizetype>((written / 2) - 1));
}

core::PdfRect rectFrom(float left, float bottom, float right, float top)
{
    // PDFium reports a box's four edges without promising an order, and a file may write them in
    // either. Normalized here rather than trusted: an inverted rectangle produces a negative size,
    // which multiplies through a scale into a negative pixel count -- and a negative pixel count
    // allocates nothing and reads as a blank page.
    const double x0 = std::min(static_cast<double>(left), static_cast<double>(right));
    const double x1 = std::max(static_cast<double>(left), static_cast<double>(right));
    const double y0 = std::min(static_cast<double>(bottom), static_cast<double>(top));
    const double y1 = std::max(static_cast<double>(bottom), static_cast<double>(top));
    return core::PdfRect{core::PdfPoint{x0, y0}, core::PdfSize{x1 - x0, y1 - y0}};
}

} // namespace

struct PdfiumDocument::Engine {
    FPDF_DOCUMENT document = nullptr;

    // THE FORM-FILL ENVIRONMENT, AND IT IS HERE PURELY SO WIDGET APPEARANCES RASTERIZE (I-21).
    //
    // PDFium splits annotation drawing in two, and the split is invisible until a form page meets
    // a reference renderer. `FPDF_RenderPageBitmap` with `FPDF_ANNOT` draws every annotation
    // EXCEPT `/Subtype /Widget`; widgets are drawn only by `FPDF_FFLDraw`, which needs a handle
    // from `FPDFDOC_InitFormFillEnvironment`. `corpus/ordinary/annots-mixed.pdf` and
    // `corpus/ordinary/forms-acroform.pdf` are the two halves of that experiment and they were
    // sitting in the corpus the whole time: nine non-widget annotations with `/AP` all rendered,
    // eleven widget annotations with `/AP` rendered as an empty page, under the same flag.
    //
    // THIS IS NOT STAGE 9 ARRIVING EARLY. `Architecture-FileMap.md`'s `PdfiumFormFill` row is
    // about *live field appearances and widget states* -- focus, typing, validation, the
    // calculation cascade -- and none of that is here. What is here is the minimum that makes a
    // widget's ALREADY-COMPOSED `/AP` stream part of the page, which is what it is under ISO
    // 32000 and what every conforming viewer draws.
    //
    // `info` MUST OUTLIVE `form`: `FPDFDOC_InitFormFillEnvironment` keeps the pointer it is given
    // rather than copying the struct, so a stack temporary here would leave PDFium reading a dead
    // frame on the first widget it draws. It is a member for that reason and no other.
    FPDF_FORMFILLINFO info{};
    FPDF_FORMHANDLE  form = nullptr;
};

PdfiumDocument::PdfiumDocument() : engine_(std::make_unique<Engine>()) {}

PdfiumDocument::~PdfiumDocument()
{
    if (engine_ && engine_->document != nullptr) {
        const std::lock_guard<std::mutex> guard(engineLock());
        // THE FORM HANDLE GOES FIRST, and the order is not cosmetic: the environment holds a
        // reference to the document, and closing the document under it is a use-after-free that
        // shows up as a crash on close rather than as anything to do with forms.
        if (engine_->form != nullptr) {
            FPDFDOC_ExitFormFillEnvironment(engine_->form);
            engine_->form = nullptr;
        }
        FPDF_CloseDocument(engine_->document);
        engine_->document = nullptr;
    }
}

void* PdfiumDocument::engineHandle() const noexcept
{
    return engine_ ? static_cast<void*>(engine_->document) : nullptr;
}

void* PdfiumDocument::formHandle() const noexcept
{
    return engine_ ? static_cast<void*>(engine_->form) : nullptr;
}

int PdfiumDocument::pageCount() const noexcept
{
    return pages_.count();
}

core::Result<std::unique_ptr<PdfiumDocument>> PdfiumDocument::open(
    const QString& path, const core::PasswordCallback& password)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return openError(core::ErrorCode::NotFound, "error.open.notFound", path);
    }

    // Read through QFile, which takes a QString and gets the Windows encoding right by
    // construction. Handing PDFium a `const char*` path is convention 1's trap one layer over --
    // this machine's code page 950 turned an emoji filename into `report-?????.pdf`.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return openError(core::ErrorCode::IoFailure, "error.open.notFound", info.fileName());
    }
    QByteArray bytes = file.readAll();
    file.close();

    return openBytes(std::move(bytes), path, password);
}

core::Result<std::unique_ptr<PdfiumDocument>> PdfiumDocument::openBytes(
    QByteArray bytes, const QString& displayPath, const core::PasswordCallback& password)
{
    const QFileInfo info(displayPath);

    if (bytes.isEmpty())
        return openError(core::ErrorCode::ParseFailure, "error.open.notPdf", info.fileName());

    if (const core::Status ready = ensure(); !ready)
        return ready.error();

    auto self = std::unique_ptr<PdfiumDocument>(new PdfiumDocument());
    self->bytes_ = std::move(bytes);
    self->path_ = displayPath;

    // THE PASSWORD LOOP. PDFium tells us a file is encrypted by failing with FPDF_ERR_PASSWORD, so
    // the first attempt is always made without one -- which is also what makes
    // `corpus/encrypted/owner-empty-user.pdf` open with no prompt at all, as it must: an empty user
    // password is the case a reader mistakes for an unencrypted file.
    QByteArray attemptPassword;
    int attempt = 0;
    for (;;) {
        {
            const std::lock_guard<std::mutex> guard(engineLock());
            self->engine_->document = FPDF_LoadMemDocument64(
                self->bytes_.constData(), static_cast<size_t>(self->bytes_.size()),
                attempt == 0 ? nullptr : attemptPassword.constData());
        }
        if (self->engine_->document != nullptr)
            break;

        const unsigned long error = FPDF_GetLastError();
        if (error != FPDF_ERR_PASSWORD) {
            // Not an encryption problem: the bytes are not a PDF, or are damaged past what even
            // PDFium's own silent repair can do -- and PDFium does not say which, so the header
            // does (I-32).
            return openError(core::ErrorCode::ParseFailure,
                             looksLikePdf(self->bytes_) ? "error.open.damaged" : "error.open.notPdf",
                             info.fileName());
        }
        if (!password) {
            // No one to ask. A batch run and a test both land here, and it is a REFUSAL rather
            // than a failure to parse -- the file is fine and we have no key.
            return openError(core::ErrorCode::PermissionDenied, "error.open.badPassword",
                             info.fileName());
        }

        ++attempt;
        const std::optional<QString> supplied = password(attempt);
        if (!supplied) {
            // The user gave up. `Cancelled`, NOT a failure to open: a cancelled prompt is not a
            // damaged file, and a message bar that said so would be lying.
            return openError(core::ErrorCode::Cancelled, "error.open.cancelled", info.fileName());
        }
        attemptPassword = supplied->toUtf8();
        attemptPassword.append('\0');

        // A bound, because `password` is supplied by a caller and a callback that always returns
        // the same wrong string would otherwise spin forever inside an open.
        if (attempt >= 16) {
            return openError(core::ErrorCode::PermissionDenied, "error.open.badPassword",
                             info.fileName());
        }
    }

    // --- The form-fill environment, for widget appearance streams only (I-21) ------------
    {
        const std::lock_guard<std::mutex> guard(engineLock());

        // VERSION 2 is what a document with no XFA wants; `FPDF_FORMFILLINFO` is otherwise all
        // zeroes, which is the whole point. Every callback in it is a way for PDFium to ask the
        // HOST to do something -- pop a dialog, set a timer, move focus -- and a read-only
        // rasterizer answers none of them. Leaving them null is supported and is what makes this
        // an appearance-stream renderer rather than an interactive form layer.
        self->engine_->info.version = 2;
        self->engine_->form =
            FPDFDOC_InitFormFillEnvironment(self->engine_->document, &self->engine_->info);

        // A DOCUMENT WITH NO `/AcroForm` STILL GETS A HANDLE, and that is correct rather than
        // wasteful: PDFium returns one regardless, `FPDF_FFLDraw` on a page with no widgets draws
        // nothing, and branching on the presence of a form dictionary here would add a second code
        // path that only the form corpus file ever takes.
        if (self->engine_->form == nullptr) {
            // NOT AN OPEN FAILURE. The page content is unaffected; what is lost is widget
            // appearances, and a document that opens without them is far better than one that
            // does not open. Logged so the degradation is visible rather than silent.
            RAWPAGE_LOG_WARNING(core::LogSubsystem::Render)
                << "form: no form-fill environment for" << info.fileName()
                << "-- widget appearance streams will not be drawn";
        }
    }

    // --- The page tree ------------------------------------------------------------------
    std::vector<core::Page> pages;
    {
        const std::lock_guard<std::mutex> guard(engineLock());
        FPDF_DOCUMENT document = self->engine_->document;
        const int count = FPDF_GetPageCount(document);
        pages.reserve(static_cast<std::size_t>(std::max(0, count)));

        for (int index = 0; index < count; ++index) {
            // Boxes are read WITHOUT loading the page where PDFium allows it, because loading
            // every page of a 900-page document to learn its size is the difference between an
            // open that feels instant and one that does not. `FPDFPage_Get*Box` needs a page, so
            // this does load -- and closes each one immediately rather than holding 900 open.
            FPDF_PAGE page = FPDF_LoadPage(document, index);
            if (page == nullptr) {
                // A page that will not load is not a reason to refuse the document: the other
                // pages are still readable, and A-21's argument about damaged files applies here
                // too. It gets a zero-sized entry so indices stay aligned with the engine's, and
                // the TRAILING run of these is dropped below.
                pages.emplace_back(index, core::PdfRect{});
                continue;
            }

            float left = 0.0F;
            float bottom = 0.0F;
            float right = 0.0F;
            float top = 0.0F;

            core::Page built(index, core::PdfRect{});
            if (FPDFPage_GetMediaBox(page, &left, &bottom, &right, &top)) {
                built.setMediaBox(rectFrom(left, bottom, right, top));
            } else {
                // No /MediaBox anywhere up the inheritance chain. US Letter is what every reader
                // substitutes, and a page of zero size would divide by zero in layout.
                built.setMediaBox(core::PdfRect{core::PdfPoint{0.0, 0.0},
                                                core::PdfSize{612.0, 792.0}});
            }
            if (FPDFPage_GetCropBox(page, &left, &bottom, &right, &top))
                built.setCropBox(rectFrom(left, bottom, right, top));
            if (FPDFPage_GetBleedBox(page, &left, &bottom, &right, &top))
                built.setBleedBox(rectFrom(left, bottom, right, top));
            if (FPDFPage_GetTrimBox(page, &left, &bottom, &right, &top))
                built.setTrimBox(rectFrom(left, bottom, right, top));
            if (FPDFPage_GetArtBox(page, &left, &bottom, &right, &top))
                built.setArtBox(rectFrom(left, bottom, right, top));

            // PDFium reports rotation in QUARTER TURNS (0..3), not degrees. Passing it straight to
            // `setRotation` would turn every 270° page into 3° -- which normalizes to 0 and shows
            // a landscape page sideways.
            built.setRotation(FPDFPage_GetRotation(page) * 90);
            built.setLabel(pageLabel(document, index));

            FPDF_ClosePage(page);
            pages.push_back(std::move(built));
        }
    }

    // **THE TREE IS AUTHORITATIVE, NOT `/Count`** -- and this is a correction to what the engine
    // reports, not a defensive flourish. `FPDF_GetPageCount` returns the `/Pages` dictionary's
    // `/Count` field verbatim, so `corpus/malformed/pages-count-wrong.pdf` -- which declares
    // `/Count 97` over three real kids -- opens through PDFium claiming **97 pages**. Measured at
    // T9; that file exists precisely to decide which of the two wins, and `PROVENANCE.md` says in
    // as many words that the answer has to be the tree.
    //
    // The page count reaches the status bar and the scrollbar before anything walks the document,
    // so believing it would give a three-page file a scrollbar 97 pages long and 94 blank pages to
    // scroll through. Only the TRAILING run of unloadable pages is dropped: a page that fails in
    // the MIDDLE keeps its placeholder, because removing it would renumber every page after it and
    // a link or an outline entry pointing at page 40 would land on page 39.
    while (!pages.empty() && pages.back().mediaBox().size.width <= 0.0
           && pages.back().mediaBox().size.height <= 0.0) {
        pages.pop_back();
    }

    // **A DOCUMENT WITH NO READABLE PAGE IS REFUSED, not opened** (Stage 2 issue I-31). There is
    // nothing to show, nothing to navigate and nothing to edit, and opening it anyway produced a
    // tab with an empty desk and no word of explanation -- the silent empty window M-07 forbids.
    // `corpus/malformed/objstm-corrupt.pdf` reaches here: PDFium opens it, and with every object in
    // the corrupt object stream unreadable its page tree has no page. Refusing HERE rather than in
    // the shell means the batch path and the CLI get the same answer as a double-click.
    if (pages.empty()) {
        RAWPAGE_LOG_WARNING(core::LogSubsystem::Render)
            << "open: refusing" << info.fileName() << "-- the engine opened it and found no page";
        return openError(core::ErrorCode::ParseFailure, "error.open.noPages", info.fileName());
    }

    self->pages_ = core::PageTree(std::move(pages));

    // --- What the first-party reader thinks of the same bytes ------------------------------
    // **PDFium repairs silently and reports nothing.** A document it opened may have had its
    // cross-reference table reconstructed entirely, and the user would never know -- which is
    // exactly the risk `Stage2-Rendering-Plan.md` §5.4 says Stage 3 has to act on before allowing
    // a plain Save. So the COS reader is run over the same buffer for its report alone; its table
    // is discarded, because rendering goes through the engine.
    //
    // A failure here is NOT a failure to open: an encrypted file is declined by `pdf/cos/` on
    // purpose (D2-6) while PDFium has just opened it successfully, and that disagreement is the
    // arrangement rather than a defect.
    {
        const std::string_view view(self->bytes_.constData(),
                                    static_cast<std::size_t>(self->bytes_.size()));
        auto inspected = cos::openWithRepair(view);
        if (inspected)
            self->repair_ = inspected.value().report;
    }

    if (self->wasRepaired()) {
        RAWPAGE_LOG_INFO(core::LogSubsystem::Render)
            << "open: repaired" << info.fileName() << "-- reconstructed"
            << static_cast<int>(self->repair_.objectsReconstructed()) << "of"
            << static_cast<int>(self->repair_.objectsTotal()) << "objects";
    }

    return self;
}

} // namespace rawpage::pdf::render
