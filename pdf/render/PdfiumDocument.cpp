#include "pdf/render/PdfiumDocument.h"

#include "core/Log.h"
#include "core/text/LayoutAnalyzer.h"
#include "core/text/PageTextModel.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/render/PdfiumInit.h"
#include "pdf/render/PdfiumTextExtractor.h"

// fpdfview.h alone is not enough, and the split is not where you would guess: the page BOXES are
// in fpdf_transformpage.h (they share a header with the functions that MOVE a page, not with the
// ones that describe it), the ROTATION is in fpdf_edit.h, and the page LABEL in fpdf_doc.h.
// Stage 5 T8's search units add the annotations (fpdf_annot.h) and the attachments (fpdf_attachment.h).
#include <fpdf_annot.h>
#include <fpdf_attachment.h>
#include <fpdf_doc.h>
#include <fpdf_edit.h>
#include <fpdf_formfill.h>
#include <fpdf_transformpage.h>
#include <fpdfview.h>

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
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
// S6 T2. PDFium's subtype constant as the model's. `core::AnnotationSubtype` happens to number its
// values as PDFium does; this maps them explicitly anyway, and the asserts are what would fail if
// either side ever renumbered.
core::AnnotationSubtype subtypeFromEngine(int subtype)
{
    static_assert(static_cast<int>(core::AnnotationSubtype::Text) == FPDF_ANNOT_TEXT);
    static_assert(static_cast<int>(core::AnnotationSubtype::Ink) == FPDF_ANNOT_INK);
    static_assert(static_cast<int>(core::AnnotationSubtype::ThreeD) == FPDF_ANNOT_THREED);
    static_assert(static_cast<int>(core::AnnotationSubtype::XfaWidget) == FPDF_ANNOT_XFAWIDGET);
    static_assert(static_cast<int>(core::AnnotationSubtype::Redact) == FPDF_ANNOT_REDACT);
    if (subtype < FPDF_ANNOT_UNKNOWN || subtype > FPDF_ANNOT_REDACT) return core::AnnotationSubtype::Unknown;
    return static_cast<core::AnnotationSubtype>(subtype);
}

// One loaded page's `/Annots`, entry by entry, as the engine reads them (D6-3). The caller holds the
// engine lock.
std::vector<core::AnnotationFact> annotationFactsOf(FPDF_PAGE page)
{
    std::vector<core::AnnotationFact> facts;
    const int count = FPDFPage_GetAnnotCount(page);
    facts.reserve(static_cast<std::size_t>(std::max(0, count)));
    for (int index = 0; index < count; ++index) {
        core::AnnotationFact fact;
        if (FPDF_ANNOTATION annotation = FPDFPage_GetAnnot(page, index)) {
            fact.dictionary = true;
            fact.subtype = subtypeFromEngine(FPDFAnnot_GetSubtype(annotation));
            FS_RECTF rect{};
            if (FPDFAnnot_GetRect(annotation, &rect))
                fact.rect = core::normalizedRect(rect.left, rect.bottom, rect.right, rect.top);
            FPDFPage_CloseAnnot(annotation);
        }
        facts.push_back(fact);
    }
    return facts;
}

QString pageLabel(FPDF_DOCUMENT document, int index)
{
    requireEngineLock();
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
    requireEngineLock();
    core::Outline outline;
    std::size_t budget = kMaxOutlineNodes;
    readOutlineLevel(document, nullptr, 0, budget, outline.roots());
    return outline;
}

core::Destinations readDestinations(FPDF_DOCUMENT document)
{
    requireEngineLock();
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
    requireEngineLock();
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
        const std::lock_guard<EngineMutex> guard(engineLock());
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

core::Result<core::PdfRect> PdfiumDocument::contentBox(int pageIndex) const
{
    const core::Page* page = pages_.at(pageIndex);
    if (page == nullptr) {
        return core::Error{core::ErrorCode::InvalidArgument,
                           QLatin1String("error.render.noSuchPage"),
                           QString::number(pageIndex)};
    }

    // THE CACHE IS CONSULTED AND WRITTEN UNDER ITS OWN LOCK, and the engine is called with that
    // lock RELEASED. Holding both across `FPDF_LoadPage` would put the map's mutex inside the
    // process-wide engine lock, and every other caller of the map would then wait behind whatever
    // PDFium is doing on the render thread. The cost is that two threads asking about the same
    // uncached page may both compute it; the answer is identical, so the loser merely writes what
    // the winner wrote.
    {
        const std::lock_guard<std::mutex> guard(contentBoxMutex_);
        const auto found = contentBoxes_.find(pageIndex);
        if (found != contentBoxes_.end()) {
            if (!found->second.has_value()) {
                return core::Error{core::ErrorCode::NotFound,
                                   QLatin1String("error.render.pageHasNoContent"),
                                   QString::number(pageIndex)};
            }
            return *found->second;
        }
    }

    std::optional<core::PdfRect> computed;
    {
        const std::lock_guard<EngineMutex> guard(engineLock());
        FPDF_DOCUMENT document = engine_->document;
        if (document == nullptr) {
            return core::Error{core::ErrorCode::Unavailable,
                               QLatin1String("error.render.documentClosed")};
        }

        FPDF_PAGE loaded = FPDF_LoadPage(document, pageIndex);
        if (loaded == nullptr) {
            // A page that will not load is the same state the page tree already records as a
            // zero-sized entry. It is not a content box of zero -- it is an absence, and the fit
            // falls back exactly as it does for an empty page.
            return core::Error{core::ErrorCode::NotFound,
                               QLatin1String("error.render.pageHasNoContent"),
                               QString::number(pageIndex)};
        }

        // THE UNION OF THE OBJECTS' BOUNDS. `FPDFPageObj_GetBounds` is the axis-aligned bound of
        // one object AFTER its own matrix, which is what a fit wants: the tighter quadpoint form
        // (`FPDFPageObj_GetRotatedBounds`) answers only for text and images, so a page with a
        // rotated path would silently lose it.
        //
        // An object whose bounds the engine declines to report is SKIPPED rather than treated as a
        // zero rectangle at the origin -- a single such object would otherwise drag the content box
        // down to the page's bottom-left corner and make Fit Visible fit the whole sheet plus the
        // margin nobody asked about.
        const int objects = FPDFPage_CountObjects(loaded);
        for (int i = 0; i < objects; ++i) {
            FPDF_PAGEOBJECT object = FPDFPage_GetObject(loaded, i);
            if (object == nullptr) continue;

            float left = 0.0F;
            float bottom = 0.0F;
            float right = 0.0F;
            float top = 0.0F;
            if (!FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)) continue;

            const core::PdfRect bounds = rectFrom(left, bottom, right, top);
            if (!computed.has_value()) {
                computed = bounds;
                continue;
            }
            const double x0 = std::min(computed->left(), bounds.left());
            const double y0 = std::min(computed->bottom(), bounds.bottom());
            const double x1 = std::max(computed->right(), bounds.right());
            const double y1 = std::max(computed->top(), bounds.top());
            computed = core::PdfRect{core::PdfPoint{x0, y0}, core::PdfSize{x1 - x0, y1 - y0}};
        }

        FPDF_ClosePage(loaded);
    }

    // **INTERSECTED WITH THE CROPBOX, for `core::Page`'s own rule.** Ink outside the crop is not
    // drawn, and a content box that included it would make Fit Visible zoom out to show something
    // the reader cannot see -- which is worse than the CropBox fit it replaces.
    // `corpus/ordinary/cropbox-margins.pdf` has its ink well inside, so this clause is exercised by
    // files that bleed rather than by the case that proves the feature.
    if (computed.has_value()) {
        const core::PdfRect clipped = core::intersect(*computed, page->cropBox());
        computed = (clipped.size.width > 0.0 && clipped.size.height > 0.0)
                       ? std::optional<core::PdfRect>{clipped}
                       : std::nullopt;
    }

    {
        const std::lock_guard<std::mutex> guard(contentBoxMutex_);
        contentBoxes_[pageIndex] = computed;
    }

    if (!computed.has_value()) {
        return core::Error{core::ErrorCode::NotFound,
                           QLatin1String("error.render.pageHasNoContent"),
                           QString::number(pageIndex)};
    }
    return *computed;
}

// =============================================================================================
// THE SEARCH UNITS -- Stage 5 T8, D5-12 (the header's declaration says what each part holds)
// =============================================================================================

namespace {

using core::text::SearchUnit;

// ISO 32000-1 12.5.6.2: the MARKUP annotations, which are what a reader calls comments. A link, a popup
// (whose text is its parent's), a widget (a field's), and the multimedia and printer's marks are not.
bool isComment(int subtype)
{
    switch (subtype) {
    case FPDF_ANNOT_TEXT:
    case FPDF_ANNOT_FREETEXT:
    case FPDF_ANNOT_LINE:
    case FPDF_ANNOT_SQUARE:
    case FPDF_ANNOT_CIRCLE:
    case FPDF_ANNOT_POLYGON:
    case FPDF_ANNOT_POLYLINE:
    case FPDF_ANNOT_HIGHLIGHT:
    case FPDF_ANNOT_UNDERLINE:
    case FPDF_ANNOT_SQUIGGLY:
    case FPDF_ANNOT_STRIKEOUT:
    case FPDF_ANNOT_STAMP:
    case FPDF_ANNOT_CARET:
    case FPDF_ANNOT_INK:
    case FPDF_ANNOT_FILEATTACHMENT:
    case FPDF_ANNOT_SOUND:
    case FPDF_ANNOT_REDACT:
        return true;
    default:
        return false;
    }
}

void outlineUnits(const std::vector<core::OutlineItem>& items, const QString& path, int depth,
                  std::vector<SearchUnit>& into)
{
    // The reader's own depth bound (`kMaxOutlineDepth`) already holds; this one only keeps the recursion
    // from trusting that.
    if (depth >= kMaxOutlineDepth) return;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const core::OutlineItem& item = items[i];
        const QString here = path.isEmpty() ? QString::number(i) : path + QLatin1Char('/') + QString::number(i);
        if (!item.title.isEmpty()) {
            SearchUnit unit;
            unit.contentClass = core::text::ContentClass::Bookmarks;
            unit.field = SearchUnit::Field::BookmarkTitle;
            unit.text = item.title;
            unit.locator = here;
            unit.destination = item.destination;
            into.push_back(std::move(unit));
        }
        outlineUnits(item.children, here, depth + 1, into);
    }
}

} // namespace

core::Status PdfiumDocument::searchUnits(int part, const std::vector<std::pair<QString, QString>>& descriptions,
                                         std::vector<core::text::SearchUnit>& into) const
{
    using core::text::ContentClass;

    // BUILT ASIDE AND SWAPPED IN AT THE END, for the extractor's reason: a failure leaves `into` untouched.
    std::vector<SearchUnit> units;

    if (part == core::text::kOutlinePart) {
        outlineUnits(outline_.roots(), QString(), 0, units);
        into = std::move(units);
        return {};
    }

    if (part == core::text::kAttachmentsPart) {
        // A BOUND ON THE COUNT, for the name tree's reason: a hostile file can claim a great many.
        constexpr int kMaxAttachments = 10000;
        int count = 0;
        {
            const std::lock_guard<EngineMutex> guard(engineLock());
            if (engine_->document == nullptr)
                return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.render.documentClosed")};
            count = std::min(FPDFDoc_GetAttachmentCount(engine_->document), kMaxAttachments);
        }
        for (int index = 0; index < count; ++index) {
            QString name;
            QByteArray bytes;
            {
                // ONE ATTACHMENT PER LOCK: its name and its bytes, and nothing else held.
                const std::lock_guard<EngineMutex> guard(engineLock());
                FPDF_ATTACHMENT attachment = FPDFDoc_GetAttachment(engine_->document, index);
                if (attachment == nullptr) continue;
                name = wideStringFrom([attachment](void* buffer, unsigned long size) {
                    return FPDFAttachment_GetName(attachment, static_cast<FPDF_WCHAR*>(buffer), size);
                });
                unsigned long length = 0;
                if (FPDFAttachment_GetFile(attachment, nullptr, 0, &length) && length > 0
                    && length < static_cast<unsigned long>(std::numeric_limits<int>::max())) {
                    bytes.resize(static_cast<qsizetype>(length));
                    unsigned long written = 0;
                    if (!FPDFAttachment_GetFile(attachment, bytes.data(), length, &written) || written != length)
                        bytes.clear();
                }
            }

            const QString locator = name;
            if (!name.isEmpty()) {
                SearchUnit unit;
                unit.contentClass = ContentClass::Attachments;
                unit.field = SearchUnit::Field::AttachmentName;
                unit.text = name;
                unit.locator = locator;
                units.push_back(std::move(unit));
            }
            // THE DESCRIPTION, JOINED BY POSITION AND CONFIRMED BY NAME (`EmbeddedFileReader`): one that
            // names another file is dropped rather than attached to this one.
            if (index < static_cast<int>(descriptions.size())) {
                const auto& [describedName, description] = descriptions[static_cast<std::size_t>(index)];
                if (describedName == name && !description.isEmpty()) {
                    SearchUnit unit;
                    unit.contentClass = ContentClass::Attachments;
                    unit.field = SearchUnit::Field::AttachmentDescription;
                    unit.text = description;
                    unit.locator = locator;
                    units.push_back(std::move(unit));
                } else if (describedName != name) {
                    RAWPAGE_LOG_WARNING(core::LogSubsystem::Render)
                        << "search units: attachment" << index << "is" << name << "to the engine and"
                        << describedName << "to the first-party reader -- its description is not searched";
                }
            }

            // AN ATTACHED PDF'S TEXT, ONE LEVEL DEEP, FROM MEMORY. `openBytes` takes the engine lock in its
            // own pieces; no password callback, so an encrypted attachment is refused, never prompted for.
            // Its attachments are never asked for -- this function is not called on it.
            if (!looksLikePdf(bytes)) continue;
            auto attached = openBytes(bytes, name, core::PasswordCallback{});
            if (!attached) {
                RAWPAGE_LOG_INFO(core::LogSubsystem::Render)
                    << "search units: attachment" << name << "is a PDF that does not open --"
                    << QString(attached.error().messageKey) << "-- searched by name only";
                continue;
            }
            // A HANDLE NO BACKEND ISSUED: the extractor refuses any other, so it can answer only for this one.
            const core::DocumentHandle self{std::numeric_limits<std::uint64_t>::max()};
            PdfiumTextExtractor extractor(*attached.value(), self);
            const int pages = attached.value()->pageCount();
            for (int page = 0; page < pages; ++page) {
                core::text::PageTextModel model(page);
                if (!extractor.extractPage(self, page, model)) continue;
                // In READING order, as the page's own search reads it, with an unmapped glyph as U+FFFD --
                // which `TextNormalizer::normalizeString` matches to nothing.
                const QString text = core::text::LayoutAnalyzer::readingText(
                    model, core::text::LayoutAnalyzer::analyze(model, nullptr));
                if (text.trimmed().isEmpty()) continue;
                SearchUnit unit;
                unit.contentClass = ContentClass::Attachments;
                unit.field = SearchUnit::Field::AttachedText;
                unit.text = text;
                unit.locator = locator;
                unit.attachedPage = page;
                units.push_back(std::move(unit));
            }        }
        into = std::move(units);
        return {};
    }

    if (pages_.at(part) == nullptr) {
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.render.noSuchPage"),
                           QString::number(part)};
    }

    {
        // ONE PAGE, AND THE WHOLE OF IT UNDER THE LOCK -- the page load, each annotation and both closes.
        const std::lock_guard<EngineMutex> guard(engineLock());
        FPDF_DOCUMENT document = engine_->document;
        if (document == nullptr)
            return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.render.documentClosed")};
        FPDF_PAGE page = FPDF_LoadPage(document, part);
        if (page == nullptr) {
            return core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.text.extractFailed"),
                               QString::number(part)};
        }
        FPDF_FORMHANDLE form = engine_->form;

        const int count = FPDFPage_GetAnnotCount(page);
        for (int index = 0; index < count; ++index) {
            FPDF_ANNOTATION annotation = FPDFPage_GetAnnot(page, index);
            if (annotation == nullptr) continue;
            const int subtype = FPDFAnnot_GetSubtype(annotation);

            SearchUnit base;
            base.page = part;
            FS_RECTF rect{};
            if (FPDFAnnot_GetRect(annotation, &rect)) {
                base.rect = rectFrom(rect.left, rect.bottom, rect.right, rect.top);
                base.hasRect = true;
            }
            const auto add = [&](ContentClass contentClass, SearchUnit::Field field, QString text, QString locator) {
                if (text.isEmpty()) return;
                SearchUnit unit = base;
                unit.contentClass = contentClass;
                unit.field = field;
                unit.text = std::move(text);
                unit.locator = std::move(locator);
                units.push_back(std::move(unit));
            };

            if (subtype == FPDF_ANNOT_WIDGET) {
                // A FIELD, through the form-fill environment open() made. Without one there are no field
                // names to read, and the page's fields are not searched (logged at open).
                if (form != nullptr) {
                    const QString fieldName = wideStringFrom([form, annotation](void* buffer, unsigned long size) {
                        return FPDFAnnot_GetFormFieldName(form, annotation, static_cast<FPDF_WCHAR*>(buffer), size);
                    });
                    const QString value = wideStringFrom([form, annotation](void* buffer, unsigned long size) {
                        return FPDFAnnot_GetFormFieldValue(form, annotation, static_cast<FPDF_WCHAR*>(buffer), size);
                    });
                    add(ContentClass::Fields, SearchUnit::Field::FieldName, fieldName, fieldName);
                    add(ContentClass::Fields, SearchUnit::Field::FieldValue, value, fieldName);
                }
            } else if (isComment(subtype)) {
                const QString locator = QString::number(index);
                add(ContentClass::Comments, SearchUnit::Field::CommentContents,
                    wideStringFrom([annotation](void* buffer, unsigned long size) {
                        return FPDFAnnot_GetStringValue(annotation, "Contents", static_cast<FPDF_WCHAR*>(buffer), size);
                    }),
                    locator);
                add(ContentClass::Comments, SearchUnit::Field::CommentSubject,
                    wideStringFrom([annotation](void* buffer, unsigned long size) {
                        return FPDFAnnot_GetStringValue(annotation, "Subj", static_cast<FPDF_WCHAR*>(buffer), size);
                    }),
                    locator);
            }
            FPDFPage_CloseAnnot(annotation);
        }
        FPDF_ClosePage(page);
    }

    into = std::move(units);
    return {};
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
    // THE STAGED OPEN, RUN TO ITS END (S5 T9): one open, whether a window asks for it in one call or a
    // folder search asks for it a piece at a time.
    Opening opening(std::move(bytes), displayPath, password);
    while (!opening.done()) {
        if (const core::Status piece = opening.step(); !piece) return piece.error();
    }
    return opening.take();
}

// =============================================================================================
// The open in pieces -- S5 T9 (the header's `Opening`)
// =============================================================================================

PdfiumDocument::Opening::Opening(QByteArray bytes, QString displayPath, core::PasswordCallback password)
    : document_(new PdfiumDocument()), password_(std::move(password))
{
    document_->bytes_ = std::move(bytes);
    document_->path_ = std::move(displayPath);
}

PdfiumDocument::Opening::~Opening() = default;

bool PdfiumDocument::Opening::done() const noexcept
{
    return stage_ == Stage::Done;
}

bool PdfiumDocument::Opening::nextNeedsEngine() const noexcept
{
    switch (stage_) {
    case Stage::Load:
    case Stage::FormEnvironment:
    case Stage::Count:
    case Stage::Pages:
    case Stage::Readers:
        return true;
    case Stage::FirstParty:
    case Stage::Done:
    case Stage::Failed:
        return false;
    }
    return false;
}

std::unique_ptr<PdfiumDocument> PdfiumDocument::Opening::take()
{
    if (stage_ != Stage::Done) return nullptr;
    return std::move(document_);
}

core::Status PdfiumDocument::Opening::step()
{
    if (stage_ == Stage::Failed) return *failure_;
    if (stage_ == Stage::Done) return {};

    PdfiumDocument* self = document_.get();
    const QFileInfo info(self->path_);
    const auto fail = [this](core::Error error) -> core::Status {
        stage_ = Stage::Failed;
        failure_ = error;
        return error;
    };

    switch (stage_) {
    case Stage::Load: {
        if (self->bytes_.isEmpty())
            return fail(openError(core::ErrorCode::ParseFailure, "error.open.notPdf", info.fileName()));

        if (const core::Status ready = ensure(); !ready) return fail(ready.error());

        // THE PASSWORD LOOP. PDFium tells us a file is encrypted by failing with FPDF_ERR_PASSWORD, so
        // the first attempt is always made without one -- which is also what makes
        // `corpus/encrypted/owner-empty-user.pdf` open with no prompt at all, as it must: an empty user
        // password is the case a reader mistakes for an unencrypted file.
        QByteArray attemptPassword;
        int attempt = 0;
        for (;;) {
            unsigned long error = 0;
            {
                // THE ERROR IS READ UNDER THE SAME LOCK AS THE LOAD (Stage 5 guide I-01): PDFium's last
                // error is process-global, and another thread's call between the two would answer for
                // a different document.
                const std::lock_guard<EngineMutex> guard(engineLock());
                self->engine_->document = FPDF_LoadMemDocument64(
                    self->bytes_.constData(), static_cast<size_t>(self->bytes_.size()),
                    attempt == 0 ? nullptr : attemptPassword.constData());
                if (self->engine_->document == nullptr) error = FPDF_GetLastError();
            }
            if (self->engine_->document != nullptr)
                break;

            if (error != FPDF_ERR_PASSWORD) {
                // Not an encryption problem: the bytes are not a PDF, or are damaged past what even
                // PDFium's own silent repair can do -- and PDFium does not say which, so the header
                // does (I-32).
                return fail(openError(core::ErrorCode::ParseFailure,
                                      looksLikePdf(self->bytes_) ? "error.open.damaged" : "error.open.notPdf",
                                      info.fileName()));
            }
            if (!password_) {
                // No one to ask. A batch run, a test and a FOLDER SEARCH (D5-13) all land here, and it
                // is a REFUSAL rather than a failure to parse -- the file is fine and we have no key.
                return fail(openError(core::ErrorCode::PermissionDenied, "error.open.badPassword",
                                      info.fileName()));
            }

            ++attempt;
            const std::optional<QString> supplied = password_(attempt);
            if (!supplied) {
                // The user gave up. `Cancelled`, NOT a failure to open: a cancelled prompt is not a
                // damaged file, and a message bar that said so would be lying.
                return fail(openError(core::ErrorCode::Cancelled, "error.open.cancelled", info.fileName()));
            }
            attemptPassword = supplied->toUtf8();
            attemptPassword.append('\0');

            // A bound, because `password` is supplied by a caller and a callback that always returns
            // the same wrong string would otherwise spin forever inside an open.
            if (attempt >= 16) {
                return fail(openError(core::ErrorCode::PermissionDenied, "error.open.badPassword",
                                      info.fileName()));
            }
        }
        stage_ = Stage::FormEnvironment;
        return {};
    }

    case Stage::FormEnvironment: {
        // --- The form-fill environment, for widget appearance streams only (I-21) ------------
        const std::lock_guard<EngineMutex> guard(engineLock());

        // VERSION 2 is what a document with no XFA wants; `FPDF_FORMFILLINFO` is otherwise all
        // zeroes, which is the whole point. Every callback in it is a way for PDFium to ask the
        // HOST to do something -- pop a dialog, set a timer, move focus -- and a read-only
        // rasterizer answers none of them. Leaving them null is supported and is what makes this
        // an appearance-stream renderer rather than an interactive form layer.
        self->engine_->info.version = 2;
        self->engine_->form = FPDFDOC_InitFormFillEnvironment(self->engine_->document, &self->engine_->info);

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
        stage_ = Stage::Count;
        return {};
    }

    case Stage::Count: {
        {
            const std::lock_guard<EngineMutex> guard(engineLock());
            count_ = FPDF_GetPageCount(self->engine_->document);
        }
        pages_.reserve(static_cast<std::size_t>(std::max(0, count_)));
        annotationFacts_.reserve(static_cast<std::size_t>(std::max(0, count_)));
        stage_ = Stage::Pages;
        return {};
    }

    case Stage::Pages: {
        // --- The page tree, `kPagesPerPiece` pages a piece ---------------------------------------
        // A LOCK PER PAGE, NOT ONE ACROSS THE WALK (Stage 5 T3). Held across all 900 pages of
        // `text-900.pdf`, the walk kept the engine for ~150 ms -- measured by `bench_pipeline --stage5`,
        // where a folder search's open made a viewport tile wait 141-260 ms. Taken per page, the render
        // worker gets the engine between two pages; nothing else can see this document yet, so the walk
        // loses nothing by being interleaved. And since S5 T9 the walk is also cut into PIECES, so a
        // folder search running it on the render thread puts a visible tile behind at most one piece.
        FPDF_DOCUMENT document = self->engine_->document;
        // BOUNDED BY TIME, NOT BY A COUNT: pages until `kPieceBudget` has passed, at least one. A count was
        // tried first -- eight pages -- and measured at up to 3.5 ms a piece on `text-900.pdf`, a page's walk
        // costing 0.2 to 0.44 ms on this machine where T3 had measured 0.16; a count is one machine's
        // arithmetic, and a budget holds on any.
        const auto began = std::chrono::steady_clock::now();
        int index = nextPage_;
        for (; index < count_; ++index) {
            if (index > nextPage_ && std::chrono::steady_clock::now() - began >= kPieceBudget) break;
            const std::lock_guard<EngineMutex> guard(engineLock());
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
                pages_.emplace_back(index, core::PdfRect{});
                annotationFacts_.emplace_back();
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
                built.setMediaBox(core::PdfRect{core::PdfPoint{0.0, 0.0}, core::PdfSize{612.0, 792.0}});
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

            // S6 T2 (D6-3): the page's `/Annots` as the engine reads it, while the page is loaded.
            // `FPDFPage_GetAnnotCount` is the ARRAY's length, whatever its entries are; an entry that
            // is not a dictionary gives no annotation handle, and is a fact in its own right.
            annotationFacts_.push_back(annotationFactsOf(page));

            FPDF_ClosePage(page);
            pages_.push_back(std::move(built));
        }
        nextPage_ = index;
        if (nextPage_ < count_) return {};

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
        while (!pages_.empty() && pages_.back().mediaBox().size.width <= 0.0
               && pages_.back().mediaBox().size.height <= 0.0) {
            pages_.pop_back();
            annotationFacts_.pop_back();
        }

        // **A DOCUMENT WITH NO READABLE PAGE IS REFUSED, not opened** (Stage 2 issue I-31). There is
        // nothing to show, nothing to navigate and nothing to edit, and opening it anyway produced a
        // tab with an empty desk and no word of explanation -- the silent empty window M-07 forbids.
        // `corpus/malformed/objstm-corrupt.pdf` reaches here: PDFium opens it, and with every object in
        // the corrupt object stream unreadable its page tree has no page. Refusing HERE rather than in
        // the shell means the batch path and the CLI get the same answer as a double-click.
        if (pages_.empty()) {
            RAWPAGE_LOG_WARNING(core::LogSubsystem::Render)
                << "open: refusing" << info.fileName() << "-- the engine opened it and found no page";
            return fail(openError(core::ErrorCode::ParseFailure, "error.open.noPages", info.fileName()));
        }

        self->pages_ = core::PageTree(std::move(pages_));
        pages_.clear();
        self->annotationFacts_ = std::move(annotationFacts_);
        annotationFacts_.clear();
        stage_ = Stage::Readers;
        return {};
    }

    case Stage::Readers: {
        // --- What the document says about itself (S4 T4) ---------------------------------------
        // Once, here. A pane that re-read these every time it was shown would be a cost nobody
        // measured (guide case A-30), and a lazy reader would have to keep the engine handle reachable
        // from `core/`, which is the layering this facade exists to prevent.
        //
        // **UNDER THE ENGINE LOCK, which they were not from Stage 4 T4 to Stage 5 T3** (Stage 5 guide
        // issue **I-01**): every call below enters PDFium, the product opens documents on the UI thread
        // while the render worker is inside the engine, and I-05 measured two threads in PDFium
        // corrupting the heap. Each reader now counts an entry made without the lock
        // (`requireEngineLock()`), and `tst_pdfiumdoc` asserts that count is zero.
        const std::lock_guard<EngineMutex> guard(engineLock());
        self->outline_ = readOutline(self->engine_->document);
        self->destinations_ = readDestinations(self->engine_->document);
        self->metadata_ = readMetadata(self->engine_->document);
        stage_ = Stage::FirstParty;
        return {};
    }

    case Stage::FirstParty: {
        // --- What the first-party reader thinks of the same bytes ------------------------------
        // **PDFium repairs silently and reports nothing.** A document it opened may have had its
        // cross-reference table reconstructed entirely, and the user would never know -- which is
        // exactly the risk `Stage2-Rendering-Plan.md` §5.4 says Stage 3 has to act on before allowing
        // a plain Save. So the COS reader is run over the same buffer for its report alone; its table
        // is discarded, because rendering goes through the engine. NO ENGINE, which is why a folder
        // search runs this piece off the render thread (S5 T9).
        //
        // A failure here is NOT a failure to open: an encrypted file is declined by `pdf/cos/` on
        // purpose (D2-6) while PDFium has just opened it successfully, and that disagreement is the
        // arrangement rather than a defect.
        const std::string_view view(self->bytes_.constData(), static_cast<std::size_t>(self->bytes_.size()));
        auto inspected = cos::openWithRepair(view);
        if (inspected)
            self->repair_ = inspected.value().report;

        if (self->wasRepaired()) {
            RAWPAGE_LOG_INFO(core::LogSubsystem::Render)
                << "open: repaired" << info.fileName() << "-- reconstructed"
                << static_cast<int>(self->repair_.objectsReconstructed()) << "of"
                << static_cast<int>(self->repair_.objectsTotal()) << "objects";
        }
        stage_ = Stage::Done;
        return {};
    }

    case Stage::Done:
    case Stage::Failed:
        break;
    }
    return {};
}

} // namespace rawpage::pdf::render
