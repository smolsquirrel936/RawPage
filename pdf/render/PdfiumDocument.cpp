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

// ---------------------------------------------------------------------------------------------
// WHAT THE DOCUMENT SAYS ABOUT ITSELF -- the outline, the named destinations and the Info
// dictionary. S4 T4, decisions D4-3 and D4-4.
// ---------------------------------------------------------------------------------------------

// UTF-16LE out of one of PDFium's two-call string APIs: ask for the size, allocate, ask again.
// `reader` is the API, so the three callers below cannot each get the terminator arithmetic
// slightly differently -- which is the shape of bug that produces a title with a trailing NUL that
// only shows up in a tooltip.
template <typename Reader>
QString wideStringFrom(Reader reader)
{
    const unsigned long bytes = reader(nullptr, 0);
    // 2 is the terminator alone: an empty string and an absent one are indistinguishable here, and
    // both mean "nothing to show".
    if (bytes <= 2) return {};
    std::vector<char> buffer(bytes);
    const unsigned long written = reader(buffer.data(), bytes);
    if (written <= 2) return {};
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()),
                              static_cast<qsizetype>((written / 2) - 1));
}

core::Destination destinationFrom(FPDF_DOCUMENT document, FPDF_DEST dest)
{
    core::Destination destination;
    if (dest == nullptr) return destination;   // unresolved: pageIndex stays -1

    // -1 for a destination naming a page that is not there -- `corpus/ordinary/dests-named.pdf`'s
    // `ghost-page` is exactly this, and it is carried rather than dropped (D4-3).
    destination.pageIndex = FPDFDest_GetDestPageIndex(document, dest);

    unsigned long parameterCount = 0;
    FS_FLOAT parameters[4] = {0, 0, 0, 0};
    switch (FPDFDest_GetView(dest, &parameterCount, parameters)) {
    case PDFDEST_VIEW_XYZ:   destination.fit = core::Destination::Fit::XYZ; break;
    case PDFDEST_VIEW_FIT:   destination.fit = core::Destination::Fit::Fit; break;
    case PDFDEST_VIEW_FITH:  destination.fit = core::Destination::Fit::FitH; break;
    case PDFDEST_VIEW_FITV:  destination.fit = core::Destination::Fit::FitV; break;
    case PDFDEST_VIEW_FITR:  destination.fit = core::Destination::Fit::FitR; break;
    case PDFDEST_VIEW_FITB:  destination.fit = core::Destination::Fit::FitB; break;
    case PDFDEST_VIEW_FITBH: destination.fit = core::Destination::Fit::FitBH; break;
    case PDFDEST_VIEW_FITBV: destination.fit = core::Destination::Fit::FitBV; break;
    default:                 destination.fit = core::Destination::Fit::Unknown; break;
    }

    // `/XYZ null null null` is legal and means *keep what you have* for each null, so the three
    // flags travel separately from their values. `FPDFDest_GetLocationInPage` answers for /XYZ; for
    // /FitH and /FitV the single parameter is the top or the left, and PDFium reports it through
    // `FPDFDest_GetView`'s parameter array instead.
    FPDF_BOOL hasX = 0;
    FPDF_BOOL hasY = 0;
    FPDF_BOOL hasZoom = 0;
    FS_FLOAT x = 0;
    FS_FLOAT y = 0;
    FS_FLOAT zoom = 0;
    if (FPDFDest_GetLocationInPage(dest, &hasX, &hasY, &hasZoom, &x, &y, &zoom) != 0) {
        destination.hasX = hasX != 0;
        destination.hasY = hasY != 0;
        destination.hasZoom = hasZoom != 0;
        destination.x = x;
        destination.y = y;
        destination.zoom = zoom;
    } else if (parameterCount >= 1) {
        switch (destination.fit) {
        case core::Destination::Fit::FitH:
        case core::Destination::Fit::FitBH:
            destination.hasY = true;
            destination.y = parameters[0];
            break;
        case core::Destination::Fit::FitV:
        case core::Destination::Fit::FitBV:
            destination.hasX = true;
            destination.x = parameters[0];
            break;
        default:
            break;
        }
    }
    return destination;
}

// The bounds on the walk below. `/First` and `/Next` are references, and a damaged or hostile file
// can point them in a circle: a reader that follows one does not fail, it HANGS, on a document that
// opens fine everywhere else. Depth is the nesting a real outline reaches many times over; the node
// cap is far above any real document's outline and far below a number that takes noticeable time.
constexpr int kMaxOutlineDepth = 32;
constexpr std::size_t kMaxOutlineNodes = 20000;

void readOutlineLevel(FPDF_DOCUMENT document, FPDF_BOOKMARK parent, int depth,
                      std::size_t& budget, std::vector<core::OutlineItem>& into)
{
    if (depth >= kMaxOutlineDepth) return;
    for (FPDF_BOOKMARK bookmark = FPDFBookmark_GetFirstChild(document, parent); bookmark != nullptr;
         bookmark = FPDFBookmark_GetNextSibling(document, bookmark)) {
        if (budget == 0) return;
        --budget;

        core::OutlineItem item;
        item.title = wideStringFrom([bookmark](void* buffer, unsigned long size) {
            return FPDFBookmark_GetTitle(bookmark, buffer, size);
        });

        // A bookmark points either at a destination or at a GoTo action, and PDFium says which by
        // answering null to the first: a file whose bookmarks are all actions would otherwise read
        // as an outline that navigates nowhere.
        FPDF_DEST dest = FPDFBookmark_GetDest(document, bookmark);
        if (dest == nullptr) {
            if (const FPDF_ACTION action = FPDFBookmark_GetAction(bookmark))
                dest = FPDFAction_GetDest(document, action);
        }
        item.destination = destinationFrom(document, dest);

        // ISO 32000-1 Table 153: a POSITIVE /Count asks for the item to appear expanded, a negative
        // one collapsed. It is what the FILE asks for at first display and never the pane's live
        // state (`core/doc/Outline.h`).
        item.openByDefault = FPDFBookmark_GetCount(bookmark) > 0;

        readOutlineLevel(document, bookmark, depth + 1, budget, item.children);
        into.push_back(std::move(item));
    }
}

core::Outline readOutline(FPDF_DOCUMENT document)
{
    core::Outline outline;
    std::size_t budget = kMaxOutlineNodes;
    readOutlineLevel(document, nullptr, 0, budget, outline.roots());
    return outline;
}

core::Destinations readDestinations(FPDF_DOCUMENT document)
{
    core::Destinations destinations;
    const int count = static_cast<int>(FPDF_CountNamedDests(document));
    for (int index = 0; index < count; ++index) {
        // `FPDF_GetNamedDest` is a two-call API with a DIFFERENT shape from the rest: the length is
        // in/out and is reported in BYTES, and a first call with a null buffer fills it in. It is
        // not `wideStringFrom`'s shape, which is why this one is written out.
        long length = 0;
        if (FPDF_GetNamedDest(document, index, nullptr, &length) == nullptr && length <= 0) continue;
        if (length <= 2) continue;

        std::vector<char> buffer(static_cast<std::size_t>(length));
        const FPDF_DEST dest = FPDF_GetNamedDest(document, index, buffer.data(), &length);
        if (length <= 2) continue;

        core::NamedDestination entry;
        entry.name = QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()),
                                        static_cast<qsizetype>((length / 2) - 1));
        // A dest the engine could not resolve still gets its row: `pageIndex` stays -1 and the
        // pane shows it as unresolvable rather than omitting it (D4-3).
        entry.destination = destinationFrom(document, dest);
        destinations.add(std::move(entry));
    }
    return destinations;
}

core::Metadata readMetadata(FPDF_DOCUMENT document)
{
    core::Metadata metadata;
    const auto field = [document](const char* key) {
        return wideStringFrom([document, key](void* buffer, unsigned long size) {
            return FPDF_GetMetaText(document, key, buffer, size);
        });
    };
    metadata.title = field("Title");
    metadata.author = field("Author");
    metadata.subject = field("Subject");
    metadata.keywords = field("Keywords");
    metadata.creator = field("Creator");
    metadata.producer = field("Producer");
    // RAW, as stored. A malformed date is shown as the file wrote it rather than invented or
    // dropped (D4-4, guide case A-28); `core::Metadata::parseDate` is what a surface calls when it
    // wants to format one.
    metadata.creationDate = field("CreationDate");
    metadata.modificationDate = field("ModDate");

    // The file HEADER's version, which no Info field carries. PDFium reports it as an integer --
    // 17 for %PDF-1.7 -- and a document whose header it could not read leaves this empty rather
    // than claiming a version.
    int version = 0;
    if (FPDF_GetFileVersion(document, &version) != 0 && version > 0) {
        metadata.pdfVersion =
            QStringLiteral("%1.%2").arg(version / 10).arg(version % 10);
    }

    // **NO XMP IS READ HERE, and that is D4-4 rather than an omission.** Reading a packet to
    // display it duplicates these fields for most documents and disagrees with them for some, and
    // which wins is a policy that belongs with the Stage 8 writer that has to keep both consistent.
    return metadata;
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

    // --- What the document says about itself (S4 T4) ---------------------------------------
    // Once, here. A pane that re-read these every time it was shown would be a cost nobody
    // measured (guide case A-30), and a lazy reader would have to keep the engine handle reachable
    // from `core/`, which is the layering this facade exists to prevent.
    self->outline_ = readOutline(self->engine_->document);
    self->destinations_ = readDestinations(self->engine_->document);
    self->metadata_ = readMetadata(self->engine_->document);

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
