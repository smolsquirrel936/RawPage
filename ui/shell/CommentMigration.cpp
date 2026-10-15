// ui/shell/CommentMigration -- implementation. Stage 6 T13. See the header for whose pipeline this is and what it owns.

#include "ui/shell/CommentMigration.h"

#include "core/Log.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/PageTree.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "core/text/DocumentComparer.h"

#include <QMetaObject>

#include <utility>

namespace rawpage::ui {

using core::LogSubsystem;

CommentMigration::CommentMigration(app::Services services, core::DocumentHandle revised, core::DocumentHandle older,
                                   QString olderPath, QObject* parent)
    : QObject(parent), services_(std::move(services)), revised_(revised), older_(older), olderPath_(std::move(olderPath))
{
    sides_[kOlder].handle = older;
    sides_[kRevised].handle = revised;
}

CommentMigration::~CommentMigration()
{
    // THE RUN FIRST: joined, so nothing of it outlives the texts it reads.
    run_.reset();
    if (subscription_ != 0) services_.documentIndex.unsubscribe(subscription_);
    if (owner_ != 0) services_.documentIndex.release(owner_);

    // THE THREE-STEP CLOSE (`DocumentWindow.h`) of the one handle this object opened -- never the tab's.
    if (older_.isValid()) {
        services_.documentIndex.detach(older_);
        services_.renderScheduler.cancelDocument(older_);
        if (!services_.renderScheduler.waitForIdle(5000)) {
            RAWPAGE_LOG_ERROR(LogSubsystem::Render) << "migrate: the render scheduler did not go idle within 5000 ms; closing anyway";
        }
        services_.renderCache.dropDocument(older_);
        if (const core::Status closed = services_.backend.close(older_); !closed) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Render) << "migrate: backend refused to close" << olderPath_ << "--" << closed.error().messageKey;
        }
    }
}

void CommentMigration::start()
{
    core::text::DocumentIndex& index = services_.documentIndex;
    owner_ = index.newOwner();
    subscription_ = index.subscribe([this](const core::text::DocumentIndex::Event& event) { onIndexEvent(event); });
    // BOTH ATTACHED BEFORE EITHER IS ASKED -- the comparison window's order: a page already held is taken at once, and
    // the run must not start because one side looked complete before the other had a page count. The revised side is
    // the tab's handle, attached by its window already; attaching it again changes nothing.
    for (Held& held : sides_) {
        const core::PageTree* pages = services_.backend.pages(held.handle);
        const int count = pages != nullptr ? pages->count() : 0;
        held.entry = index.attach(services_.backend.documentId(held.handle), held.handle, count);
        held.text.assign(static_cast<std::size_t>(count), nullptr);
        held.textHeld = 0;
    }
    running_ = true;
    for (const int side : {kOlder, kRevised}) {
        Held& held = sides_[static_cast<std::size_t>(side)];
        for (int page = 0; page < static_cast<int>(held.text.size()); ++page) {
            if (index.request(owner_, held.entry, page, core::render::RenderPriority::Background)) take(side, page);
        }
    }
    take(kOlder, -1);   // nothing to take; starts the run when every page was already here
}

void CommentMigration::onIndexEvent(const core::text::DocumentIndex::Event& event)
{
    if (event.kind != core::text::DocumentIndex::Event::Kind::PageArrived) return;
    // Both sides may be one entry -- a revision migrated from itself.
    for (const int side : {kOlder, kRevised}) {
        if (sides_[static_cast<std::size_t>(side)].entry == event.entry) take(side, event.page);
    }
}

void CommentMigration::take(int side, int page)
{
    Held& held = sides_[static_cast<std::size_t>(side)];
    if (page >= 0 && page < static_cast<int>(held.text.size()) && !held.text[static_cast<std::size_t>(page)]) {
        std::shared_ptr<const core::text::IndexedPage> indexed = services_.documentIndex.page(held.entry, page);
        if (!indexed) return;
        held.text[static_cast<std::size_t>(page)] = std::move(indexed);
        ++held.textHeld;
    }
    if (!running_ || run_) return;
    const int total = static_cast<int>(sides_[kOlder].text.size() + sides_[kRevised].text.size());
    const int done = sides_[kOlder].textHeld + sides_[kRevised].textHeld;
    emit progress(done, total, false);
    if (done < total) return;
    startRun();
}

void CommentMigration::startRun()
{
    // EACH PAGE'S FACTS FROM THE ENGINE'S TREE, ITS TEXT FROM THE INDEX, its reading made on the run's thread -- and at
    // TEXT ONLY: an alignment is all the migrator asks of the comparer, and no raster is drawn.
    std::array<core::text::CompareDocument, 2> documents;
    for (const int side : {kOlder, kRevised}) {
        const Held& held = sides_[static_cast<std::size_t>(side)];
        const core::PageTree* pages = services_.backend.pages(held.handle);
        for (int page = 0; page < static_cast<int>(held.text.size()); ++page) {
            core::text::ComparePage compared;
            if (const core::Page* info = pages != nullptr ? pages->at(page) : nullptr) {
                compared.cropBox = info->cropBox();
                compared.rotation = info->rotation();
                compared.label = info->label();
            }
            documents[static_cast<std::size_t>(side)].push_back(std::move(compared));
        }
    }
    core::text::CompareOptions options;
    options.sensitivity = core::text::CompareSensitivity::TextOnly;
    run_ = std::make_unique<core::text::ComparisonRun>(std::move(documents[kOlder]), std::move(documents[kRevised]), options);
    run_->setReadingSource([older = sides_[kOlder].text, revised = sides_[kRevised].text](core::text::CompareSide side, int page)
                               -> std::shared_ptr<const core::text::PageReading> {
        const auto& text = side == core::text::CompareSide::Base ? older : revised;
        if (page < 0 || page >= static_cast<int>(text.size()) || !text[static_cast<std::size_t>(page)]) return nullptr;
        return text[static_cast<std::size_t>(page)]->reading();
    });
    run_->setProgressHandler([this](int done, int total) { emit progress(done, total, true); });
    run_->setFinishedHandler([this](const core::text::ComparisonResult& result) { onRunFinished(result); });
    // FROM THE RUN'S THREAD, carrying nothing: delivered on this object's event loop.
    run_->setWakeHandler([this] {
        QMetaObject::invokeMethod(this, [this] { if (run_) run_->deliverReady(); }, Qt::QueuedConnection);
    });
    run_->start();
}

void CommentMigration::onRunFinished(const core::text::ComparisonResult& result)
{
    if (result.cancelled) {
        finish(core::Error{core::ErrorCode::Cancelled, QLatin1String("error.migrate.cancelled"), olderPath_});
        return;
    }
    const core::AnnotationStore* store = services_.backend.annotations(older_);
    if (store == nullptr || !store->status()) {
        finish(store != nullptr ? store->status().error()
                                : core::Error{core::ErrorCode::Unavailable, QLatin1String("error.migrate.noComments"), olderPath_});
        return;
    }
    const core::Fdf original = store->exportFdf(store->topLevel());
    if (original.annotations.empty()) {
        finish(core::Error{core::ErrorCode::NotFound, QLatin1String("error.migrate.noComments"), olderPath_});
        return;
    }
    core::CommentMigrator::Pages base;
    core::CommentMigrator::Pages revised;
    for (const auto& page : sides_[kOlder].text) base.push_back(page ? page->reading() : nullptr);
    for (const auto& page : sides_[kRevised].text) revised.push_back(page ? page->reading() : nullptr);
    core::MigrationResult migrated = core::CommentMigrator::migrate(original, base, revised, result.pages);
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "migrate:" << migrated.count(core::MigrationEntry::Outcome::Placed) << "placed,"
                                       << migrated.count(core::MigrationEntry::Outcome::Reported) << "reported, from" << olderPath_;
    finish(std::move(migrated));
}

void CommentMigration::finish(core::Result<core::MigrationResult> outcome)
{
    running_ = false;
    outcome_ = std::move(outcome);
    emit finished();
}

} // namespace rawpage::ui
