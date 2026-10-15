// ui/shell/CommentMigration -- one migration of comments from an older revision into the tab in front, run without
// holding the UI thread. Stage 6 T13, decision D6-21 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// THE COMPARISON'S PIPELINE, REUSED, NOT COPIED
// ===========================================================================================
// What the migrator needs is what the comparison window gathers (S5 T13, `ui/viewer/ComparisonView`): both
// documents' text, extracted on the ENGINE THREAD through `core/text/DocumentIndex` at `Background`, and their page
// alignment, computed by `core/text/ComparisonRun` on a thread of its own at *text only*. This object asks for the
// first and starts the second, and when the run is delivered -- on the UI thread's event loop -- reads the older
// document's comments as an exchange (`AnnotationStore::exportFdf`) and hands all of it to `core/doc/CommentMigrator`,
// which is pure and quick. The outcome is the shell's to merge into the tab's journal (`MainWindow`): this object
// changes no document.
//
// ===========================================================================================
// WHAT IT OWNS
// ===========================================================================================
// The OLDER document was opened for the migration (`MainWindow::migrateCommentsFrom`), a handle of its own -- never a
// tab's, so nothing reaches a journal -- and this object CLOSES it, with the three-step close `DocumentWindow.h`
// states (out of the index, cancel and drain, drop its tiles, close). The REVISED document is the tab's handle: read,
// never detached or closed here.

#ifndef RAWPAGE_UI_SHELL_COMMENTMIGRATION_H
#define RAWPAGE_UI_SHELL_COMMENTMIGRATION_H

#include "app/Services.h"
#include "core/Result.h"
#include "core/backend.h"
#include "core/doc/CommentMigrator.h"
#include "core/text/DocumentIndex.h"

#include <QObject>
#include <QString>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace rawpage::core::text {
class ComparisonRun;
struct ComparisonResult;
}

namespace rawpage::ui {

class CommentMigration final : public QObject {
    Q_OBJECT
public:
    CommentMigration(app::Services services, core::DocumentHandle revised, core::DocumentHandle older,
                     QString olderPath, QObject* parent = nullptr);
    ~CommentMigration() override;

    CommentMigration(const CommentMigration&) = delete;
    CommentMigration& operator=(const CommentMigration&) = delete;

    // Asks for both documents' text; the run starts when the last page arrives.
    void start();

    const QString& olderPath() const noexcept { return olderPath_; }
    core::DocumentHandle revised() const noexcept { return revised_; }
    bool isFinished() const noexcept { return outcome_.has_value(); }
    // The migration -- what landed where, and what was reported -- or why there is none. Set once, before `finished`.
    const std::optional<core::Result<core::MigrationResult>>& outcome() const noexcept { return outcome_; }

signals:
    // Pages read of both documents, then pairs compared -- `total` fixed for each.
    void progress(int done, int total, bool comparing);
    void finished();

private:
    enum Side { kOlder = 0, kRevised = 1 };
    struct Held {
        core::DocumentHandle handle;
        core::text::DocumentIndex::EntryId entry = 0;
        std::vector<std::shared_ptr<const core::text::IndexedPage>> text;
        int textHeld = 0;
    };

    void onIndexEvent(const core::text::DocumentIndex::Event& event);
    void take(int side, int page);
    void startRun();
    void onRunFinished(const core::text::ComparisonResult& result);
    void finish(core::Result<core::MigrationResult> outcome);

    app::Services services_;
    core::DocumentHandle revised_;
    core::DocumentHandle older_;
    QString olderPath_;
    std::array<Held, 2> sides_;
    core::text::DocumentIndex::Owner owner_ = 0;
    core::text::DocumentIndex::SubscriptionToken subscription_ = 0;
    std::unique_ptr<core::text::ComparisonRun> run_;
    bool running_ = false;
    std::optional<core::Result<core::MigrationResult>> outcome_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_COMMENTMIGRATION_H
