// A `core::IDocumentBackend` for the widget suite. Stage 2 T9.
//
// WHY A FAKE RATHER THAN THE REAL ONE, and it is not a convenience. `tests/ui` links
// `rawpage_core` and `rawpage_ui` and nothing else (D1-4); the real backend lives in
// `rawpage_pdfium`, which this target must not link for the same reason `tests/unit` must not.
// More importantly, **guide case A-47 is about the SHELL'S POLICY, not about PDFium**: the claim
// is that `openFiles()` asks the backend rather than the filename. A fake is what lets a case
// decide, precisely and without a real PDF, whether the shell consulted it at all.
//
// It records every path it was asked about, which is the half that makes the assertion possible:
// "the file was refused" and "the backend was never asked" look identical from outside, and only
// the second is the bug A-47 exists to catch.

#ifndef RAWPAGE_TESTS_UI_FAKEBACKEND_H
#define RAWPAGE_TESTS_UI_FAKEBACKEND_H

#include "core/doc/Destinations.h"
#include "core/doc/Metadata.h"
#include "core/doc/Outline.h"
#include "core/backend.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/doc/DocumentId.h"
#include "core/doc/Permissions.h"
#include "core/doc/RepairReport.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace rawpage::uitest {

// Stage 3 T10: a sink that accepts every rotation and records the last one per page. What it wrote is
// `tests/unit`'s and `tests/render`'s to judge; the widget suite needs a document it may edit.
class FakeSink final : public core::MutationSink {
public:
    core::Status setPageRotation(int pageIndex, int degrees) override
    {
        rotations[pageIndex] = ((degrees % 360) + 360) % 360;
        ++calls;
        return {};
    }
    std::map<int, int> rotations;
    int calls = 0;
};

class FakeBackend final : public core::IDocumentBackend {
public:
    // STAGE 3 T10 -- EDITING. Off by default, which keeps every Stage 2 case's document read-only exactly
    // as before. On, a document is assessed as the first-party reader would assess `editableReading`
    // (`Read` edits; `Encrypted` refuses by key, as the shipped backend refuses the encrypted corpus),
    // takes its identity from the file on disk, and saves by writing the opened bytes plus a marker line.
    bool editable = false;
    core::CosReading editableReading = core::CosReading::Read;
    bool failWrites = false;     // a save's write step fails by key -- `IoFailure`, THE PLACE (Save-As is offered)
    bool failVerify = false;     // the composed bytes fail their own re-read -- `Corrupted`, OUR BYTES (refused)
    int writes = 0;
    int adoptions = 0;

    // What to answer for a given path. The default opens everything, so a harness that does not
    // care about opening writes nothing; a case that does sets this.
    std::function<bool(const QString&)> accepts = [](const QString&) { return true; };

    // What a refusal says, for a case that needs a particular one -- a cancelled prompt must not
    // raise the could-not-open banner that a damaged file must (Stage 2 issue I-32).
    core::ErrorCode refusalCode = core::ErrorCode::ParseFailure;
    const char* refusalKey = "error.open.notPdf";

    // Every path `open()` was asked about, in order.
    QStringList asked;

    // Called at the top of `open()`, before anything is decided -- which is the moment the REAL
    // backend would invoke the password callback and put a modal prompt on screen. A case that
    // needs to know the state of the window at that moment sets it (Stage 2 issue I-30).
    std::function<void(const QString&)> onOpen;
    int closes = 0;

    // How many A4 pages an accepted document has. Zero gives a document with a real handle and no
    // pages -- a state the viewer must survive, and one the SHIPPED backend never hands over: it
    // refuses a document with no readable page (Stage 2 issue I-31). This comment used to say no
    // real PDF reaches it; `corpus/malformed/objstm-corrupt.pdf` did, for as long as nothing refused.
    int pagesPerDocument = 3;

    std::array<bool, core::kBackendInterfaceCount> implementedInterfaces() const override
    {
        // The shape the shipped backend reports after Stage 2, so a widget written against this
        // fake cannot come to depend on capabilities the real one does not have.
        return {true, true, false, false, false, false, false};
    }

    core::Result<core::DocumentHandle> open(const QString& path,
                                            const core::PasswordCallback&) override
    {
        asked << path;
        if (onOpen) onOpen(path);
        if (!accepts(path)) {
            // The FILE NAME as context, as `PdfiumDocument` gives it -- not the path.
            return core::Error{refusalCode, QLatin1String(refusalKey), QFileInfo(path).fileName()};
        }
        const core::DocumentHandle handle{static_cast<std::uint64_t>(asked.size())};

        std::vector<core::Page> built;
        built.reserve(static_cast<std::size_t>(pagesPerDocument));
        for (int i = 0; i < pagesPerDocument; ++i) {
            built.emplace_back(i, core::PdfRect{core::PdfPoint{0.0, 0.0},
                                                core::PdfSize{595.276, 841.89}});
        }
        trees[handle.id] = std::make_unique<core::PageTree>(std::move(built));
        if (editable) {
            auto entry = std::make_unique<EditState>();
            std::vector<core::CosPageFacts> facts;
            for (const core::Page& page : trees[handle.id]->pages())
                facts.push_back(core::CosPageFacts{static_cast<int>(page.rotation()), page.mediaBox().size});
            entry->permissions = core::Permissions::assess(*trees[handle.id], editableReading, facts);
            entry->sourcePath = path;
            entry->id = identityOf(path);
            edits[handle.id] = std::move(entry);
        }

        return handle;
    }

    core::Status close(core::DocumentHandle handle) override
    {
        ++closes;
        trees.erase(handle.id);
        edits.erase(handle.id);
        return {};
    }

    // A REAL `PageTree`, because the viewer's whole job is arithmetic over one -- a null here would
    // make every layout case assert that nothing was laid out. A4 at 595.276 x 841.89 points, which
    // is what `corpus/ordinary/` is generated at.
    const core::PageTree* pages(core::DocumentHandle handle) const override
    {
        const auto found = trees.find(handle.id);
        return found == trees.end() ? nullptr : found->second.get();
    }

    // A REAL, EMPTY report by default -- which is the state "this file needed no repair", and is
    // what makes "opening a clean file shows no bar" assertable. A case that wants the banner
    // fills `repairs` in, and one that wants a backend with no report at all clears it.
    const core::RepairReport* repairReport(core::DocumentHandle handle) const override
    {
        return trees.count(handle.id) != 0 ? &repairs : nullptr;
    }

    core::RepairReport repairs;

    // Null for both unless `editable` (Stage 3 T10). A null permissions pointer hydrates to "not assessed",
    // which refuses editing -- the safe reading for a fake whose pages come from nowhere.
    const core::Permissions* permissions(core::DocumentHandle handle) const override
    {
        const auto found = edits.find(handle.id);
        return found == edits.end() ? nullptr : &found->second->permissions;
    }
    const core::DocumentId* documentId(core::DocumentHandle handle) const override
    {
        const auto found = edits.find(handle.id);
        return found == edits.end() ? nullptr : &found->second->id;
    }
    // S4 T4 (D4-3, D4-4). This double reads no file, so it carries EMPTY models rather than null:
    // null means *not a handle of mine*, and empty means *this document has none*, which is the
    // distinction `core/backend.h` asks callers to make. A case that needs an outline fills these.
    const core::Outline* outline(core::DocumentHandle) const override
    {
        ++outlineReads;
        return &outline_;
    }
    const core::Destinations* destinations(core::DocumentHandle) const override
    {
        ++destinationReads;
        return &destinations_;
    }
    const core::Metadata* metadata(core::DocumentHandle) const override { return &metadata_; }

    // S4 T6: how many times a caller ASKED for each model. Guide case A-30 claims the readers are
    // hydrated once at open and that showing a pane re-reads nothing; the `pdf/render/` half of
    // that is met by construction (the `FPDF_DOCUMENT` never leaves that directory), and this
    // counter is what lets the WIDGET half be asserted rather than argued -- a pane that rebuilt
    // its tree on every show would be visible here as a growing number. `mutable` because the
    // accessors are const by interface, which is itself the point: fetching a model is a read of
    // something already in memory.
    mutable int outlineReads = 0;
    mutable int destinationReads = 0;

    core::Outline outline_;
    core::Destinations destinations_;
    core::Metadata metadata_;


    // Stage 3 T10: the opened file's bytes plus a line naming the save, so a saved file differs from the one
    // before it -- enough for `SaveCoordinator`'s identity check and for the chrome's dirty marker.
    core::Status writeRevision(core::DocumentHandle handle, const QString& temporaryPath) override
    {
        const auto found = edits.find(handle.id);
        if (found == edits.end() || !found->second->permissions.canEditPages())
            return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), QString()};
        if (failWrites)
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.save.writeFailed"), temporaryPath};
        // What `pdf/ops/RevisionWriter` returns when the file it wrote does not read back as the document
        // it composed: no other path would take those bytes either, so this one is never a Save-As offer.
        if (failVerify)
            return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.save.verifyFailed"),
                               QStringLiteral("pages")};
        QFile source(found->second->sourcePath);
        QByteArray bytes = source.open(QIODevice::ReadOnly) ? source.readAll() : QByteArray();
        bytes += "\n% saved revision " + QByteArray::number(++writes) + "\n";
        QFile out(temporaryPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size())
            return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.save.writeFailed"), temporaryPath};
        return {};
    }
    core::Status adoptRevision(core::DocumentHandle handle, const QString& path) override
    {
        const auto found = edits.find(handle.id);
        if (found == edits.end())
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.open.notFound"), path};
        found->second->sourcePath = path;
        found->second->id = identityOf(path);
        repairs = core::RepairReport{};          // a saved file needs no repair
        ++adoptions;
        return {};
    }

    core::IPageRasterizer* rasterizer(core::DocumentHandle) override { return nullptr; }
    core::ITextExtractor* textExtractor(core::DocumentHandle) override { return nullptr; }
    core::MutationSink* mutationSink(core::DocumentHandle handle) override
    {
        const auto found = edits.find(handle.id);
        return found != edits.end() && found->second->permissions.canEditPages() ? &found->second->sink : nullptr;
    }
    FakeSink* sinkFor(core::DocumentHandle handle)
    {
        const auto found = edits.find(handle.id);
        return found == edits.end() ? nullptr : &found->second->sink;
    }
    core::ISigner* signer(core::DocumentHandle) override { return nullptr; }
    core::IOcrEngine* ocrEngine() override { return nullptr; }
    core::IIndexer* indexer() override { return nullptr; }

private:
    struct EditState {
        core::Permissions permissions;
        core::DocumentId id;
        FakeSink sink;
        QString sourcePath;
    };
    static core::DocumentId identityOf(const QString& path)
    {
        QFile file(path);
        const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("fake");
        return core::DocumentId::of(path, QFileInfo(path).lastModified().toMSecsSinceEpoch(), bytes);
    }
    std::map<std::uint64_t, std::unique_ptr<EditState>> edits;
    // `mutable` because `pages()` is const and `open()` is not: the map is the fake's storage, and
    // the constness belongs to the interface rather than to this object's bookkeeping.
    mutable std::map<std::uint64_t, std::unique_ptr<core::PageTree>> trees;
};

} // namespace rawpage::uitest

#endif // RAWPAGE_TESTS_UI_FAKEBACKEND_H
