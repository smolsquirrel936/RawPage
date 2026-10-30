// tst_annotdisplay -- how an edit reaches the pixels. Stage 6 T4, decisions D6-2 and D6-10
// (`Stage6-Annotations-Plan.md` §3), guide cases A-34 (in part), A-35, A-36, A-37 and A-39
// (`Stage6-Annotations-Testing-Guide.md` §5.6).
//
// THE INVARIANT D6-2 BUYS, AND THE CASE THAT HOLDS IT: what the screen shows IS what Save writes. After
// every command the backend composes the update Save would write, verifies it as Save does, and the
// engine reloads `original ⧺ update` -- so a page rasterized from the LIVE engine and the same page
// rasterized from the SAVED FILE reopened in a fresh engine must be the same pixels, exactly. Any
// difference means the screen and the file disagree.
//
// WHY HERE: it needs the shipped engine (PDFium), the shipped backend's reload and the real scheduler;
// this target links all three. The journal is driven directly and its show step is set exactly as
// `ui/shell/DocumentWindow` sets it -- `IDocumentBackend::showEdits` for the tab's handle.
//
// WHAT IT DOES NOT COVER (convention 19):
//   * A-34's TOOLS: every annotation here is created by constructing a command, because T4 builds no
//     tool -- the guide's "created through its tool" is each tool task's (T6 ... T14), in
//     `tests/composition/tst_annotink.cpp`. And Highlight is the one type the sink writes at T4;
//   * `DocumentWindow` itself: its show handler is the same one line, and a companion's reload is
//     exercised here by handing the backend two handles, not by a second window;
//   * the status bar's word on a refused edit (A-39): no UI path runs an annotation command before T5;
//   * a search finding the highlight's text (A-36's last clause): comments in an open document are
//     searched from the store at T9 (D6-11), not before.

#include <catch2/catch_test_macros.hpp>

#include "app/BackendRasterizer.h"
#include "app/BackendTextExtractor.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Annotation.h"
#include "core/doc/Document.h"
#include "core/doc/SaveCoordinator.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/text/DocumentIndex.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/ops/RevisionWriter.h"
#include "pdf/render/FontResolverBridge.h"
#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumInit.h"
#include "pdf/render/PdfiumRasterizer.h"
#include "platform/win/WinDurableFiles.h"
#include "platform/win/WinFontResolver.h"
#include "tests/bench/HeapCensus.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <map>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace rawpage;
using pdf::DocumentBackendImpl;

namespace {

QString corpusFile(const char* relative)
{
    return QDir(QStringLiteral(RAWPAGE_CORPUS_DIR)).filePath(QString::fromUtf8(relative));
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// THE PRODUCT'S ASSEMBLY: the font bridge installed, as `app/CompositionRoot` installs it -- without it
// PDFium's own fallback draws an unembedded non-standard font differently on every open in one process
// (Stage 3 guide I-09), which would be the engine's noise, not anything shown or saved.
struct FontBridge {
    FontBridge() { REQUIRE(pdf::render::FontResolverBridge::install(resolver)); }
    ~FontBridge() { pdf::render::FontResolverBridge::uninstall(); }
    platform::win::WinFontResolver resolver;
};

// One copy of a corpus file opened by the SHIPPED backend, its journal SHOWN as `DocumentWindow` shows it
// (unless `shown` is false -- the reload skipped, which is A-34's control).
struct Live {
    DocumentBackendImpl backend;
    platform::win::WinDurableFiles durableFiles;
    QString path;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    std::unique_ptr<core::SaveCoordinator> coordinator;
};

std::unique_ptr<Live> openLive(const QString& source, const QString& copyPath, bool shown = true)
{
    auto live = std::make_unique<Live>();
    REQUIRE(QFile::copy(source, copyPath));
    QFile::setPermissions(copyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    live->path = copyPath;
    auto handle = live->backend.open(copyPath, nullptr);
    REQUIRE(handle);
    live->handle = handle.value();
    REQUIRE(live->backend.mutationSink(live->handle) != nullptr);
    core::Document document;
    REQUIRE(document.hydrate(live->backend, live->handle));
    live->journal = std::make_unique<core::CommandJournal>(std::move(document), live->backend.mutationSink(live->handle));
    if (shown) {
        Live* self = live.get();
        live->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); },
                                  [self] { return self->backend.settleEdits(self->handle); });
    }
    live->coordinator = std::make_unique<core::SaveCoordinator>(live->backend, &live->durableFiles);
    return live;
}

QString saveOrSaveAs(Live& live, const QString& saveAsPath)
{
    auto result = live.coordinator->save(*live.journal, live.path);
    INFO("save error: " << (result ? std::string() : std::string(result.error().messageKey.data()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    if (result.value().kind == core::SaveOutcome::Kind::Saved) return live.path;
    auto as = live.coordinator->saveAs(*live.journal, saveAsPath);
    REQUIRE(as);
    REQUIRE(as.value().kind == core::SaveOutcome::Kind::Saved);
    return saveAsPath;
}

core::PageRotation turns(int degrees)
{
    return core::rotateBy(core::PageRotation::None, ((degrees % 360) + 360) % 360 / 90);
}

// One whole page, at `scale` (1.0 is 72 dpi), under a VIEW rotation, carrying the unsaved-rotation delta
// a tile carries in the product -- the model's rotation minus what `pages()` says the engine loaded.
core::render::RenderRequest pageRequest(const core::IDocumentBackend& backend, core::DocumentHandle handle, int page,
                                double scale, core::PageRotation view, core::PageRotation delta,
                                core::Revision revision = core::Revision::initial())
{
    const core::PageTree* tree = backend.pages(handle);
    REQUIRE(tree != nullptr);
    const core::Page* entry = tree->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfSize size = entry->displaySize();
    const bool swap = core::swapsAxes(core::combine(view, delta));
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = scale;
    request.revision = revision;
    request.viewRotation = view;
    request.pageRotationDelta = delta;
    request.flags = core::render::RenderFlags::Annotations;   // as every product tile asks
    request.tile = core::render::TileRect{0, 0,
                                          std::max(1, static_cast<int>(std::ceil((swap ? size.height : size.width) * scale))),
                                          std::max(1, static_cast<int>(std::ceil((swap ? size.width : size.height) * scale)))};
    return request;
}

core::RasterImage render(core::IDocumentBackend& backend, core::DocumentHandle handle, int page, double scale,
                         core::PageRotation view, core::PageRotation delta)
{
    core::IPageRasterizer* rasterizer = backend.rasterizer(handle);
    REQUIRE(rasterizer != nullptr);
    auto image = rasterizer->raster(pageRequest(backend, handle, page, scale, view, delta));
    REQUIRE(image);
    return image.value();
}

// The delta the product would carry for `page`: the MODEL's rotation over the engine tree's (D3-15).
core::PageRotation deltaOf(const Live& live, int page)
{
    const int model = static_cast<int>(live.journal->document().pages().at(page)->rotation());
    const int engine = static_cast<int>(live.backend.pages(live.handle)->at(page)->rotation());
    return turns(model - engine);
}

core::RasterImage renderLive(Live& live, int page, double scale, core::PageRotation view)
{
    return render(live.backend, live.handle, page, scale, view, deltaOf(live, page));
}

// Channels that differ, compared by hand into a number: a failed CHECK on two QByteArrays crashes
// Catch2's console reporter (Stage 3 guide I-09).
qsizetype differingBytes(const core::RasterImage& a, const core::RasterImage& b)
{
    if (a.width != b.width || a.height != b.height) return std::max(a.pixels.size(), b.pixels.size());
    qsizetype differing = 0;
    for (qsizetype i = 0; i < a.pixels.size(); ++i)
        if (a.pixels[i] != b.pixels[i]) ++differing;
    return differing;
}

// A highlight in the upper middle of `page`'s crop box, where every file used here has room.
core::Annotation highlightOn(const core::IDocumentBackend& backend, core::DocumentHandle handle, int page,
                             double shift = 0.0)
{
    const core::Page* entry = backend.pages(handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfRect box = entry->cropBox();
    const double left = box.left() + box.size.width * 0.15 + shift;
    const double right = left + box.size.width * 0.5;
    const double top = box.bottom() + box.size.height * 0.7;
    const double bottom = top - 18.0;
    core::Annotation a = core::newAnnotation(core::AnnotationSubtype::Highlight, page, QStringLiteral("T4 Display"),
                                             1791504000);
    a.quadPoints = {left, top, right, top, left, bottom, right, bottom};
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {1, 1, 0};
    a.opacity = 0.5;
    a.contents = QStringLiteral("shown is saved");
    return a;
}

core::Status addHighlight(Live& live, int page, double shift = 0.0)
{
    return live.journal->run(std::make_unique<core::AddAnnotationCommand>(
        std::vector<core::Annotation>{highlightOn(live.backend, live.handle, page, shift)}));
}

struct View {
    double scale;
    core::PageRotation rotation;
    const char* name;
};
// §3.2's three: 72 dpi, 144 dpi, and a 90° view rotation.
const View kViews[] = {{1.0, core::PageRotation::None, "72 dpi"},
                       {2.0, core::PageRotation::None, "144 dpi"},
                       {1.0, core::PageRotation::Clockwise90, "72 dpi turned 90"}};

// The saved file, reopened FRESH, drawn as `live` draws `page` in each view: the number of differing
// channels, summed. Zero is the invariant.
qsizetype differenceFromSaved(Live& live, const QString& saved, int page)
{
    DocumentBackendImpl fresh;
    auto handle = fresh.open(saved, nullptr);
    REQUIRE(handle);
    qsizetype total = 0;
    for (const View& view : kViews) {
        INFO("view: " << view.name);
        const core::RasterImage shown = renderLive(live, page, view.scale, view.rotation);
        const core::RasterImage file = render(fresh, handle.value(), page, view.scale, view.rotation, core::PageRotation::None);
        const qsizetype differing = differingBytes(shown, file);
        CHECK(shown.width == file.width);
        CHECK(shown.height == file.height);
        total += differing;
    }
    CHECK(fresh.close(handle.value()));
    return total;
}

} // namespace

// =============================================================================================
// A-34 -- what is shown is what is saved (§3.2), for the one type T4 can write, through commands
// =============================================================================================

TEST_CASE("a highlight shown before the save is the page the saved file draws", "[render][annotdisplay][A-34]")
{
    FontBridge fonts;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    struct Target {
        const char* file;
        int page;
    };
    // A page with no annotations, one with nine, an /Annots inside an object stream, the four
    // rotations, a direct annotation dictionary, page 450 of a 900-page file.
    const Target targets[] = {{"ordinary/text-a4.pdf", 0},          {"ordinary/annots-mixed.pdf", 0},
                              {"ordinary/objstm-xrefstream.pdf", 0}, {"ordinary/annots-rotated.pdf", 0},
                              {"ordinary/annots-rotated.pdf", 1},    {"ordinary/annots-rotated.pdf", 2},
                              {"ordinary/annots-rotated.pdf", 3},    {"ordinary/annots-direct.pdf", 0},
                              {"ordinary/text-900.pdf", 449}};
    int n = 0;
    for (const Target& target : targets) {
        INFO("file: " << target.file << " page " << target.page + 1);
        const QString copy = dir.filePath(QStringLiteral("a34-%1.pdf").arg(n++));
        auto live = openLive(corpusFile(target.file), copy);
        const core::RasterImage before = renderLive(*live, target.page, 1.0, core::PageRotation::None);

        REQUIRE(addHighlight(*live, target.page));
        // THE CHANGE IS IN THE PIXELS (rule 1) -- a reload that drew nothing new would pass the
        // equality below on a file whose save also wrote nothing visible.
        CHECK(differingBytes(before, renderLive(*live, target.page, 1.0, core::PageRotation::None)) > 0);

        const QString saved = saveOrSaveAs(*live, copy + QStringLiteral(".saved-as.pdf"));
        CHECK(differenceFromSaved(*live, saved, target.page) == 0);
    }
}

TEST_CASE("without the reload the screen and the saved file disagree", "[render][annotdisplay][A-34][control]")
{
    // A-34'S CONTROL: the same edit with the show step left out -- the engine left on the old bytes. The
    // live page then does NOT change, and differs from the saved file in every view.
    FontBridge fonts;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto live = openLive(corpusFile("ordinary/text-a4.pdf"), dir.filePath(QStringLiteral("control.pdf")), false);
    const core::RasterImage before = renderLive(*live, 0, 1.0, core::PageRotation::None);
    REQUIRE(addHighlight(*live, 0));
    CHECK(differingBytes(before, renderLive(*live, 0, 1.0, core::PageRotation::None)) == 0);
    const QString saved = saveOrSaveAs(*live, dir.filePath(QStringLiteral("control-as.pdf")));

    DocumentBackendImpl fresh;
    auto handle = fresh.open(saved, nullptr);
    REQUIRE(handle);
    for (const View& view : kViews) {
        INFO("view: " << view.name);
        CHECK(differingBytes(renderLive(*live, 0, view.scale, view.rotation),
                             render(fresh, handle.value(), 0, view.scale, view.rotation, core::PageRotation::None))
              > 0);
    }
    CHECK(fresh.close(handle.value()));
}

TEST_CASE("a rotation and a highlight on one page are drawn as the saved file draws them", "[render][annotdisplay][A-34]")
{
    // THE RASTERIZER'S CORRECTION (D6-2): after a reload the engine holds the composed `/Rotate`, while
    // the product's delta is relative to the tree it opened -- without the correction this page turns
    // twice. Both orders, on a page at /Rotate 0 and one at /Rotate 90.
    FontBridge fonts;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    int n = 0;
    for (const char* file : {"ordinary/text-a4.pdf", "ordinary/annots-rotated.pdf"}) {
        const int page = QString::fromUtf8(file).contains(QStringLiteral("rotated")) ? 1 : 0;
        for (const bool rotateFirst : {true, false}) {
            INFO("file: " << file << (rotateFirst ? ", rotated then highlighted" : ", highlighted then rotated"));
            const QString copy = dir.filePath(QStringLiteral("rot-%1.pdf").arg(n++));
            auto live = openLive(corpusFile(file), copy);
            if (rotateFirst) REQUIRE(live->journal->run(std::make_unique<core::RotatePageCommand>(page, 90)));
            REQUIRE(addHighlight(*live, page));
            if (!rotateFirst) REQUIRE(live->journal->run(std::make_unique<core::RotatePageCommand>(page, 90)));
            CHECK(deltaOf(*live, page) == core::PageRotation::Clockwise90);
            const QString saved = saveOrSaveAs(*live, copy + QStringLiteral(".saved-as.pdf"));
            CHECK(differenceFromSaved(*live, saved, page) == 0);
        }
    }
}

// =============================================================================================
// A-35 -- an undo shows the page as it was
// =============================================================================================

TEST_CASE("an undone highlight shows the page as the file draws it and a redo shows it again", "[render][annotdisplay][A-35]")
{
    FontBridge fonts;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    int n = 0;
    for (const char* file : {"ordinary/text-a4.pdf", "ordinary/annots-mixed.pdf", "ordinary/annots-rotated.pdf"}) {
        INFO("file: " << file);
        auto live = openLive(corpusFile(file), dir.filePath(QStringLiteral("a35-%1.pdf").arg(n++)));
        const int page = QString::fromUtf8(file).contains(QStringLiteral("rotated")) ? 2 : 0;
        std::vector<core::RasterImage> original;
        for (const View& view : kViews) original.push_back(renderLive(*live, page, view.scale, view.rotation));

        REQUIRE(addHighlight(*live, page));
        const core::RasterImage added = renderLive(*live, page, 1.0, core::PageRotation::None);
        CHECK(differingBytes(original[0], added) > 0);

        REQUIRE(live->journal->undo());
        for (std::size_t v = 0; v < std::size(kViews); ++v) {
            INFO("view: " << kViews[v].name);
            CHECK(differingBytes(original[v], renderLive(*live, page, kViews[v].scale, kViews[v].rotation)) == 0);
        }
        REQUIRE(live->journal->redo());
        CHECK(differingBytes(added, renderLive(*live, page, 1.0, core::PageRotation::None)) == 0);
    }
}

// =============================================================================================
// A-34 and A-35 for the IN-PLACE edits (Stage 6 T5): a move, a recolour, new text and a lock are drawn as
// the saved file draws them, and an undo draws the page as it was
// =============================================================================================

TEST_CASE("an annotation edited in place is shown as the saved file draws it and undone as the file was",
          "[render][annotdisplay][A-34][A-35]")
{
    FontBridge fonts;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    constexpr std::int64_t kAt = 1791590400;
    struct Edit {
        const char* file;
        int page;
        const char* what;
        bool visible;   // whether the edit changes what the page draws -- a lock and a note's text do not
    };
    // Every kind on annots-every-type moved with its producer's appearance; a direct dictionary promoted; a
    // highlight from the file recoloured (redrawn by our builder); a square's text and a lock (drawn the same).
    const Edit edits[] = {{"ordinary/annots-every-type.pdf", 0, "move-all", true},
                          {"ordinary/annots-direct.pdf", 0, "move-all", true},
                          {"ordinary/annots-rotated.pdf", 1, "move-all", true},
                          {"ordinary/annots-mixed.pdf", 0, "recolour-highlight", true},
                          {"ordinary/annots-mixed.pdf", 0, "retext-and-lock", false}};
    int n = 0;
    for (const Edit& edit : edits) {
        INFO("file: " << edit.file << " page " << edit.page + 1 << " edit " << edit.what);
        const QString copy = dir.filePath(QStringLiteral("t5-edit-%1.pdf").arg(n++));
        auto live = openLive(corpusFile(edit.file), copy);
        std::vector<core::RasterImage> original;
        for (const View& view : kViews) original.push_back(renderLive(*live, edit.page, view.scale, view.rotation));

        const core::AnnotationStore& store = live->journal->document().annotations();
        std::vector<core::AnnotationId> owned;
        for (const std::size_t index : store.onPage(edit.page)) {
            if (store.at(index).subtype != core::AnnotationSubtype::Popup) owned.push_back(store.at(index).id);
        }
        REQUIRE_FALSE(owned.empty());
        int steps = 0;
        if (QLatin1String(edit.what) == QLatin1String("move-all")) {
            REQUIRE(live->journal->run(std::make_unique<core::MoveAnnotationCommand>(owned, 9.0, -14.0, kAt)));
            ++steps;
        } else if (QLatin1String(edit.what) == QLatin1String("recolour-highlight")) {
            std::vector<core::AnnotationId> highlights;
            for (const std::size_t index : store.onPage(edit.page)) {
                if (store.at(index).subtype == core::AnnotationSubtype::Highlight) highlights.push_back(store.at(index).id);
            }
            REQUIRE_FALSE(highlights.empty());
            core::AnnotationAppearance appearance;
            appearance.color = std::vector<double>{0.1, 0.4, 1.0};
            REQUIRE(live->journal->run(std::make_unique<core::SetAnnotationAppearanceCommand>(highlights, appearance, kAt)));
            ++steps;
        } else {
            std::optional<core::Annotation> square;
            for (const std::size_t index : store.onPage(edit.page)) {
                if (store.at(index).subtype == core::AnnotationSubtype::Square) square = store.at(index);
            }
            REQUIRE(square);
            square->contents = QStringLiteral("edited in place");
            square->modified = core::PdfDate::fromUtcSeconds(kAt);
            REQUIRE(live->journal->run(std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{*square})));
            REQUIRE(live->journal->run(std::make_unique<core::LockAnnotationCommand>(
                std::vector<core::AnnotationId>{square->id}, core::AnnotationFlag::Locked, 0, kAt)));
            steps += 2;
        }
        // THE CHANGE IS IN THE PIXELS where it should be, and NOT where it should not (rule 1).
        const qsizetype changed = differingBytes(original[0], renderLive(*live, edit.page, 1.0, core::PageRotation::None));
        if (edit.visible) CHECK(changed > 0);
        else CHECK(changed == 0);

        const QString saved = saveOrSaveAs(*live, copy + QStringLiteral(".saved-as.pdf"));
        CHECK(differenceFromSaved(*live, saved, edit.page) == 0);

        // A-35: undone, after the save, the page is the original file's to the byte, in every view.
        for (int i = 0; i < steps; ++i) REQUIRE(live->journal->undo());
        for (std::size_t v = 0; v < std::size(kViews); ++v) {
            INFO("view: " << kViews[v].name);
            CHECK(differingBytes(original[v], renderLive(*live, edit.page, kViews[v].scale, kViews[v].rotation)) == 0);
        }
    }
}

// =============================================================================================
// A-39 -- an edit that cannot be shown is refused, and the page keeps drawing
// =============================================================================================

TEST_CASE("an edit whose composition fails verify is refused by key and the page keeps drawing", "[render][annotdisplay][A-39]")
{
    FontBridge fonts;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto live = openLive(corpusFile("ordinary/text-a4.pdf"), dir.filePath(QStringLiteral("a39.pdf")));
    const core::RasterImage before = renderLive(*live, 0, 1.0, core::PageRotation::None);
    const QByteArray modelBefore = live->journal->document().modelHash();
    const auto recordBefore = live->backend.reloadRecord(live->handle);
    REQUIRE(recordBefore);

    // A-30's fault: the update's `/Annots` renamed, which every check Stage 3 had would pass.
    const std::size_t original = static_cast<std::size_t>(readAll(live->path).size());
    pdf::ops::RevisionWriter::setComposeFaultForTesting([original](QByteArray& bytes) {
        const qsizetype at = bytes.indexOf("/Annots", static_cast<qsizetype>(original));
        if (at >= 0) bytes[at + 1] = 'X';
    });
    // S7 T4: the verify runs off the journal's thread, so the run RETURNS and the refusal is the settle's -- which
    // takes the edit back. Cleared before the settle on purpose: the verify checks the seam as it stood when the
    // update was composed (`RevisionWriter::verifyUpdate`), not when the thread reaches it.
    REQUIRE(addHighlight(*live, 0));
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    const core::Status refused = live->journal->settle();

    REQUIRE_FALSE(refused);
    CHECK(std::string(refused.error().messageKey.data()) == "error.edit.notShown");
    CHECK(refused.error().context.startsWith(QStringLiteral("error.save.verifyFailed")));
    // NOT LEFT IN THE JOURNAL, and the model exactly as it was.
    CHECK_FALSE(live->journal->canUndo());
    CHECK_FALSE(live->journal->isDirty());
    CHECK(live->journal->document().modelHash() == modelBefore);
    // NOTHING QUEUED, and the engine draws what it drew.
    const auto recordAfter = live->backend.reloadRecord(live->handle);
    REQUIRE(recordAfter);
    CHECK(recordAfter->pendingGeneration == recordBefore->pendingGeneration);
    CHECK(differingBytes(before, renderLive(*live, 0, 1.0, core::PageRotation::None)) == 0);
    CHECK(recordAfter->reloads == recordBefore->reloads);

    // THE CONTROL: the same edit without the fault is made, and shown.
    REQUIRE(addHighlight(*live, 0));
    CHECK(differingBytes(before, renderLive(*live, 0, 1.0, core::PageRotation::None)) > 0);
}

TEST_CASE("a revision the engine cannot load leaves it drawing the one it had", "[render][annotdisplay][A-39]")
{
    // THE ENGINE'S OWN REFUSAL, below the first-party check: bytes PDFium will not open. The swap does
    // not happen, the failure is counted, and the page is drawn as before -- and a good revision after
    // it is swapped in as usual.
    FontBridge fonts;
    auto document = pdf::render::PdfiumDocument::openBytes(readAll(corpusFile("ordinary/text-a4.pdf")),
                                                           QStringLiteral("text-a4.pdf"), {});
    REQUIRE(document);
    pdf::render::PdfiumRasterizer rasterizer(*document.value());
    const auto draw = [&]() {
        core::render::RenderRequest request;
        request.document = core::DocumentHandle{1};
        request.pageIndex = 0;
        request.deviceScale = 0.5;
        request.revision = core::Revision::initial();
        request.flags = core::render::RenderFlags::Annotations;
        const core::PdfSize size = document.value()->pages().at(0)->displaySize();
        request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(size.width * 0.5)),
                                              static_cast<int>(std::ceil(size.height * 0.5))};
        auto image = rasterizer.raster(request);
        REQUIRE(image);
        return image.value();
    };
    const core::RasterImage before = draw();

    document.value()->show(QByteArray("this is not a PDF at all"), QByteArray());
    const core::RasterImage after = draw();
    const auto stats = document.value()->reloadStats();
    CHECK(stats.failures == 1);
    CHECK(stats.reloads == 0);
    CHECK(differingBytes(before, after) == 0);

    // And back on its own bytes -- no work, since it never left them.
    document.value()->show(document.value()->bytes(), QByteArray());
    (void)draw();
    CHECK(document.value()->reloadStats().reloads == 0);
}

// =============================================================================================
// A-37 -- the reload happens on the render thread, between jobs, and copies nothing
// =============================================================================================

TEST_CASE("every reload runs on the render thread between jobs and shares the original bytes", "[render][annotdisplay][A-37]")
{
    FontBridge fonts;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = corpusFile("ordinary/pages-900.pdf");
    const auto fileSize = static_cast<double>(QFileInfo(source).size());
    auto live = openLive(source, dir.filePath(QStringLiteral("a37.pdf")));

    core::render::RenderCache cache;
    core::render::RenderScheduler scheduler(
        [&live]() -> std::unique_ptr<core::IPageRasterizer> { return std::make_unique<app::BackendRasterizer>(live->backend); },
        &cache);
    live->journal->subscribeMutated([&](const core::Scope& scope) {
        scheduler.documentChanged(live->handle, scope, live->journal->revision(), live->backend.pages(live->handle));
    });

    // A BURST WHILE TILES ARE IN FLIGHT: three pages' tiles queued, a command, the next three queued...
    // Every reload is then taken by whichever job enters the engine first after it -- on the worker.
    const int page = 449;
    const auto queuePages = [&]() {
        for (int p = page - 1; p <= page + 1; ++p) {
            const core::render::RenderRequest request = pageRequest(live->backend, live->handle, p, 0.5, core::PageRotation::None,
                                                            core::PageRotation::None, live->journal->revision());
            REQUIRE(scheduler.enqueue(request, core::render::RenderPriority::Visible).isValid());
        }
    };
    constexpr int kCommands = 20;
    for (int i = 0; i < kCommands; ++i) {
        queuePages();
        REQUIRE(addHighlight(*live, page, i % 5));
        scheduler.deliverReady();
    }
    queuePages();
    REQUIRE(scheduler.waitForIdle(60000));
    scheduler.deliverReady();

    const auto record = live->backend.reloadRecord(live->handle);
    REQUIRE(record);
    INFO("reloads " << record->reloads << ", coalesced " << record->coalesced << ", failures " << record->failures);
    CHECK(record->reloads >= 1);
    CHECK(record->reloads <= kCommands);
    CHECK(record->failures == 0);
    // ON THE RENDER THREAD, never this one -- the case drives the journal from here, as the UI thread does.
    CHECK(record->lastThread != std::this_thread::get_id());
    // THE NEWEST SHOWN: the last command's revision is what the engine draws.
    CHECK(record->shownGeneration == record->pendingGeneration);
    // BETWEEN JOBS: no job failed against a document a swap closed, and nothing entered the engine
    // without its lock (the swap included).
    CHECK(scheduler.stats().failed == 0);
    CHECK(pdf::render::unlockedEngineEntries() == 0);
    // NO COPY: the engine reads the original from the very buffer the backend holds.
    CHECK(record->engineBase == record->newestOnDisk);

    // AND THE LIVE HEAP DOES NOT GROW BY A COPY PER RELOAD: add and undo, alternately, so the update is
    // the same size each time, each a reload taken by a job on the worker. Measured after a warm-up pair.
    const auto cycle = [&]() {
        REQUIRE(addHighlight(*live, page + 1));
        queuePages();
        REQUIRE(scheduler.waitForIdle(60000));
        scheduler.deliverReady();
        REQUIRE(live->journal->undo());
        queuePages();
        REQUIRE(scheduler.waitForIdle(60000));
        scheduler.deliverReady();
    };
    cycle();
    const long long reloadsBefore = live->backend.reloadRecord(live->handle)->reloads;
    const std::size_t heapBefore = bench::heapCensus().busyBytes;
    constexpr int kCycles = 10;
    for (int i = 0; i < kCycles; ++i) cycle();
    const std::size_t heapAfter = bench::heapCensus().busyBytes;
    const long long reloads = live->backend.reloadRecord(live->handle)->reloads - reloadsBefore;
    REQUIRE(reloads > 0);
    const double growthPerReload = (static_cast<double>(heapAfter) - static_cast<double>(heapBefore)) / static_cast<double>(reloads);
    INFO("live heap " << heapBefore << " -> " << heapAfter << " bytes over " << reloads << " reloads: "
                      << growthPerReload << " bytes a reload, against " << fileSize * 0.1);
    CHECK(growthPerReload < fileSize * 0.1);

    scheduler.cancelDocument(live->handle);
    scheduler.waitForIdle(10000);
}

// =============================================================================================
// A-36 -- only the scope is invalidated, and an annotation keeps the page's text (D6-10)
// =============================================================================================

TEST_CASE("an annotation re-renders its page alone and keeps the page's text", "[render][annotdisplay][A-36]")
{
    FontBridge fonts;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto live = openLive(corpusFile("ordinary/text-900.pdf"), dir.filePath(QStringLiteral("a36.pdf")));

    core::render::RenderCache cache;
    core::render::RenderScheduler scheduler(
        [&live]() -> std::unique_ptr<core::IPageRasterizer> { return std::make_unique<app::BackendRasterizer>(live->backend); },
        [&live]() -> std::unique_ptr<core::ITextExtractor> { return std::make_unique<app::BackendTextExtractor>(live->backend); },
        &cache, core::render::RenderScheduler::Config{});
    core::text::DocumentIndex index(scheduler);
    const core::text::DocumentIndex::EntryId entry =
        index.attach(live->backend.documentId(live->handle), live->handle, live->backend.pages(live->handle)->count());
    REQUIRE(entry != 0);
    // AS `DocumentWindow` SUBSCRIBES: the scheduler and the index both told the command's scope.
    live->journal->subscribeMutated([&](const core::Scope& scope) {
        scheduler.documentChanged(live->handle, scope, live->journal->revision(), live->backend.pages(live->handle));
        index.invalidate(entry, scope);
    });

    const auto pump = [&]() {
        REQUIRE(scheduler.waitForIdle(60000));
        scheduler.deliverReady();
    };
    std::vector<core::RasterImage> delivered(3);
    const auto token = scheduler.subscribeReady([&](const core::render::RenderRequest& request, const core::RasterImage& image) {
        if (request.pageIndex >= 448 && request.pageIndex <= 450) delivered[static_cast<std::size_t>(request.pageIndex - 448)] = image;
    });
    const auto queuePages = [&]() {
        for (int p = 448; p <= 450; ++p) {
            REQUIRE(scheduler.enqueue(pageRequest(live->backend, live->handle, p, 0.5, core::PageRotation::None,
                                                  core::PageRotation::None, live->journal->revision()),
                                      core::render::RenderPriority::Visible).isValid());
        }
    };

    // The three pages' TEXT in the index -- the search's state -- and their tiles in the cache.
    // `request` answers true only when the page is already held, so its false is the ask being made.
    const core::text::DocumentIndex::Owner owner = index.newOwner();
    for (int p = 448; p <= 450; ++p) REQUIRE_FALSE(index.request(owner, entry, p, core::render::RenderPriority::Visible));
    for (int i = 0; i < 1000 && (!index.page(entry, 448) || !index.page(entry, 449) || !index.page(entry, 450)); ++i) pump();
    REQUIRE(index.page(entry, 449) != nullptr);
    queuePages();
    pump();
    const core::RasterImage before449 = delivered[1];

    const auto beforeEdit = scheduler.stats();
    REQUIRE(addHighlight(*live, 449));   // page 450, 0-based 449
    queuePages();
    pump();
    const auto afterEdit = scheduler.stats();

    // PAGES 449 AND 451 FROM THE CACHE, page 450 rendered again -- and drawn with the highlight.
    CHECK(afterEdit.servedFromCache - beforeEdit.servedFromCache == 2);
    CHECK(afterEdit.started - beforeEdit.started == 1);
    CHECK(differingBytes(before449, delivered[1]) > 0);
    // THE TEXT STAYS: page 450's extraction is still held, and no text job was asked for.
    CHECK(index.page(entry, 449) != nullptr);
    CHECK(afterEdit.textEnqueued == beforeEdit.textEnqueued);

    // THE CONTROL: a CONTENT scope on the same page drops its text, as a rotation's does.
    index.invalidate(entry, core::Scope::page(449));
    CHECK(index.page(entry, 449) == nullptr);

    scheduler.unsubscribeReady(token);
    index.release(owner);
    index.detach(live->handle);
    scheduler.cancelDocument(live->handle);
    scheduler.waitForIdle(10000);
}

// =============================================================================================
// The named work of A2 at Stage 6 (T4): only the tiles an edit reaches are drawn again -- and every
// tile of the page, kept or drawn again, is still what the saved file draws
// =============================================================================================

TEST_CASE("every tile of an edited page is what the saved file draws though only the reached ones are redrawn",
          "[render][annotdisplay][A-34][A-36]")
{
    // THE STALE-TILE CASE for D6-10's rectangles: a page cut into 256-pixel tiles at 2 px/pt, every tile
    // rendered and cached through the shipped scheduler, a highlight added, every tile asked for again --
    // most served from the cache under the new revision, a few drawn again -- and EACH compared with the
    // same tile drawn from the saved file reopened fresh. A rectangle placed wrongly in any rotation
    // leaves a cached tile without the highlight beside one that has it, and that tile differs.
    FontBridge fonts;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    struct Target {
        const char* file;
        int page;
        core::PageRotation view;
        bool rotateFirst;   // an unsaved rotation of the page before the highlight: a delta on every tile
    };
    const Target targets[] = {{"ordinary/text-a4.pdf", 0, core::PageRotation::None, false},
                              {"ordinary/text-a4.pdf", 0, core::PageRotation::Clockwise90, false},
                              {"ordinary/annots-rotated.pdf", 1, core::PageRotation::None, false},
                              {"ordinary/annots-rotated.pdf", 3, core::PageRotation::Half, false},
                              {"ordinary/text-a4.pdf", 0, core::PageRotation::None, true}};
    int n = 0;
    for (const Target& target : targets) {
        INFO("file: " << target.file << " page " << target.page + 1 << ", view " << static_cast<int>(target.view)
                      << (target.rotateFirst ? ", rotated first" : ""));
        const QString copy = dir.filePath(QStringLiteral("tiles-%1.pdf").arg(n++));
        auto live = openLive(corpusFile(target.file), copy);
        if (target.rotateFirst) REQUIRE(live->journal->run(std::make_unique<core::RotatePageCommand>(target.page, 90)));

        core::render::RenderCache cache;
        core::render::RenderScheduler scheduler(
            [&live]() -> std::unique_ptr<core::IPageRasterizer> { return std::make_unique<app::BackendRasterizer>(live->backend); },
            &cache);
        live->journal->subscribeMutated([&](const core::Scope& scope) {
            scheduler.documentChanged(live->handle, scope, live->journal->revision(), live->backend.pages(live->handle));
        });
        std::map<std::pair<int, int>, core::RasterImage> delivered;
        const auto token = scheduler.subscribeReady([&](const core::render::RenderRequest& request, const core::RasterImage& image) {
            delivered[{request.tile.x, request.tile.y}] = image;
        });

        // The page's tiles at 2 px/pt, under the view rotation and the unsaved delta the product carries.
        const core::render::RenderRequest whole = pageRequest(live->backend, live->handle, target.page, 2.0, target.view,
                                                              deltaOf(*live, target.page), live->journal->revision());
        std::vector<core::render::RenderRequest> tiles;
        for (int y = 0; y < whole.tile.height; y += 256) {
            for (int x = 0; x < whole.tile.width; x += 256) {
                core::render::RenderRequest tile = whole;
                tile.tile = core::render::TileRect{x, y, std::min(256, whole.tile.width - x), std::min(256, whole.tile.height - y)};
                tiles.push_back(tile);
            }
        }
        const auto askAll = [&](bool deliver) {
            delivered.clear();
            for (core::render::RenderRequest tile : tiles) {
                tile.revision = live->journal->revision();
                REQUIRE(scheduler.enqueue(tile, core::render::RenderPriority::Visible).isValid());
            }
            REQUIRE(scheduler.waitForIdle(60000));
            if (!deliver) return;
            scheduler.deliverReady();
            REQUIRE(delivered.size() == tiles.size());
        };
        // THE FIRST PASS IS DRAWN AND NOT YET DELIVERED when the edit lands -- every tile FINISHED and
        // waiting for the pump, the window in which a tile the change reached, if it were kept and re-keyed,
        // would reach the cache under the new revision with the old pixels. The ones it did not reach are
        // kept (I-06) and cached under the new revision; the ones it reached are dropped.
        askAll(false);

        const auto before = scheduler.stats();
        REQUIRE(addHighlight(*live, target.page));
        scheduler.deliverReady();
        askAll(true);
        const auto after = scheduler.stats();
        const long long drawnAgain = after.started - before.started;
        INFO(tiles.size() << " tiles, " << drawnAgain << " drawn again");
        CHECK(drawnAgain >= 1);
        CHECK(drawnAgain < static_cast<long long>(tiles.size()));   // ONLY the reached ones
        CHECK(after.servedFromCache - before.servedFromCache == static_cast<long long>(tiles.size()) - drawnAgain);
        CHECK(after.rekeyed - before.rekeyed == static_cast<long long>(tiles.size()) - drawnAgain);   // kept, finished

        // EVERY TILE against the saved file reopened fresh, drawn with no delta: its tree carries the rotation.
        const QString saved = saveOrSaveAs(*live, copy + QStringLiteral(".saved-as.pdf"));
        DocumentBackendImpl fresh;
        auto handle = fresh.open(saved, nullptr);
        REQUIRE(handle);
        core::IPageRasterizer* rasterizer = fresh.rasterizer(handle.value());
        REQUIRE(rasterizer != nullptr);
        qsizetype differing = 0;
        for (const core::render::RenderRequest& tile : tiles) {
            core::render::RenderRequest again = tile;
            again.document = handle.value();
            again.pageRotationDelta = core::PageRotation::None;
            again.revision = core::Revision::initial();
            auto image = rasterizer->raster(again);
            REQUIRE(image);
            differing += differingBytes(delivered.at({tile.tile.x, tile.tile.y}), image.value());
        }
        CHECK(differing == 0);
        CHECK(fresh.close(handle.value()));

        scheduler.unsubscribeReady(token);
        scheduler.cancelDocument(live->handle);
        scheduler.waitForIdle(10000);
    }
}
