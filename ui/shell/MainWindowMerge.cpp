// ui/shell/MainWindowMerge -- `merge.documents`: the dialog, the run off the UI thread, and the new document. Stage 7
// T10 (`Stage7-Assembly-Plan.md` §6, D7-16, D7-33; guide A-66 ... A-72, M-03). A translation unit of `MainWindow` for
// `MainWindowPages.cpp`'s reason: one feature's registration and handlers in one place.
//
// A MERGE IS NOT A COMMAND (D7-5): it reads files and changes no open document, so it needs no document in front --
// `merge.documents` is Global -- only a person to answer the dialog (convention 16). The work is
// `IDocumentBackend::mergeFiles` on `core::MergeRunner`'s thread; this window shows its progress, cancels it, and opens
// what it made as an untitled tab exactly as an extract's is opened (`DocumentWindow::Assembled`, D7-30(b)).

#include "ui/shell/MainWindow.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/doc/Merge.h"
#include "core/doc/MergeRunner.h"
#include "core/settings/Settings.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/StatusBar.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>

namespace rawpage::ui {

using core::ActionId;
using core::KeyContext;
using core::LogSubsystem;

namespace {

bool aPersonCanRead()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

QString nameOf(const QString& path)
{
    return QFileInfo(path).fileName();
}

} // namespace

bool MainWindow::isMerging() const
{
    return mergeRunner_ && mergeRunner_->isRunning();
}

void MainWindow::cancelMerge()
{
    if (!isMerging()) return;
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "merge: cancel asked";
    mergeRunner_->cancel();
    if (status_) status_->setMessage(catalog::translate(catalog::key::MergeCancelling));
}

core::Status MainWindow::mergeDocuments(const MergeDialog::Choice& choice)
{
    const auto refuse = [this](const core::Error& error) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "merge refused --" << error.messageKey << error.context;
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return core::Status(error);
    };
    if (!mergeRunner_) mergeRunner_ = std::make_unique<core::MergeRunner>(services_.backend);
    std::vector<QString> paths;
    for (const core::MergeInput& input : choice.inputs) paths.push_back(input.path);

    QPointer<MainWindow> self(this);
    const core::Status started = mergeRunner_->start(
        choice.inputs, choice.options,
        [self](int done, int total) {
            if (!self) return;
            if (auto* progress = qobject_cast<QProgressDialog*>(self->mergeProgress_.data())) {
                progress->setMaximum(total);
                progress->setValue(done);
            }
            if (self->status_) self->status_->setMessage(catalog::translate(catalog::key::MergeWorking).arg(done).arg(total));
        },
        [self, paths](core::Result<core::MergedDocument> merged) {
            if (self) self->finishMerge(std::move(merged), paths);
        });
    if (!started) return refuse(started.error());

    // THE PROGRESS WINDOW: non-modal, so `setValue` never spins an event loop of its own; its Cancel stops the merge at
    // its next step (§3.5's second).
    auto* progress = new QProgressDialog(catalog::translate(catalog::key::MergeProgressTitle), catalog::translate(catalog::key::MergeCancel), 0,
                                         static_cast<int>(choice.inputs.size()) + 2, this);
    progress->setObjectName(QStringLiteral("mergeProgress"));
    progress->setWindowTitle(catalog::translate(catalog::key::MergeTitle));
    progress->setWindowModality(Qt::NonModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setAttribute(Qt::WA_DeleteOnClose);
    connect(progress, &QProgressDialog::canceled, this, &MainWindow::cancelMerge);
    mergeProgress_ = progress;
    progress->setValue(0);
    if (status_) status_->setMessage(catalog::translate(catalog::key::MergeWorking).arg(0).arg(static_cast<int>(choice.inputs.size()) + 2));
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "merge:" << static_cast<int>(choice.inputs.size()) << "file(s) asked, bookmarks"
                                       << QString::fromUtf8(core::mergeBookmarksName(choice.options.bookmarks).data())
                                       << (choice.options.renameCollidingFields ? "rename" : "merge") << "fields"
                                       << (choice.options.separatorPages ? "with separators" : "");
    return {};
}

void MainWindow::finishMerge(core::Result<core::MergedDocument> merged, const std::vector<QString>& paths)
{
    if (mergeProgress_) {
        mergeProgress_->close();
        mergeProgress_ = nullptr;
    }
    const auto say = [this](const QString& text) {
        if (status_) status_->setMessage(text);
    };
    if (!merged) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "merge ended --" << merged.error().messageKey << merged.error().context;
        say(catalog::translateError(QString(merged.error().messageKey), merged.error().context));
        return;
    }
    const core::MergeReport& report = merged.value().report;
    // The new document's Save As proposes the first input's folder.
    const QString base = paths.empty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                       : QFileInfo(paths.front()).absolutePath();
    const QString suggested = QDir(base).filePath(catalog::translate(catalog::key::MergeSuggestedName) + QLatin1String(".pdf"));
    core::Result<core::DocumentHandle> opened = services_.backend.openBytes(merged.value().bytes, catalog::translate(catalog::key::DocumentUntitled),
                                                                            core::PasswordCallback{}, core::BytesOrigin::Assembly);
    if (!opened) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "merge: the merged document did not open --" << opened.error().messageKey;
        say(catalog::translateError(QString(opened.error().messageKey), opened.error().context));
        return;
    }
    adoptDocument(new DocumentWindow(services_, opened.value(), DocumentWindow::Assembled{suggested}));
    updateDocumentChrome();

    // WHAT HAPPENED, on the status bar -- and line by line where somebody can read a report.
    const auto renamed = std::count_if(report.fields.begin(), report.fields.end(), [](const core::FieldCollision& c) { return !c.merged(); });
    const auto fused = static_cast<long>(report.fields.size()) - static_cast<long>(renamed);
    QString told = catalog::translate(catalog::key::MergeDone).arg(report.inputsMerged).arg(report.pageCount);
    if (!report.skipped.empty()) told += QLatin1Char(' ') + catalog::translate(catalog::key::MergeSkipped).arg(static_cast<int>(report.skipped.size()));
    if (renamed > 0) told += QLatin1Char(' ') + catalog::translate(catalog::key::MergeFieldsRenamedCount).arg(static_cast<int>(renamed));
    if (fused > 0) told += QLatin1Char(' ') + catalog::translate(catalog::key::MergeFieldsMergedCount).arg(static_cast<int>(fused));
    say(told);

    QStringList lines;
    for (const core::MergeSkip& skipped : report.skipped) {
        lines << catalog::translate(catalog::key::MergeReportSkipped)
                     .arg(QDir::toNativeSeparators(skipped.path), catalog::translateError(skipped.reasonKey, skipped.context));
    }
    // The input a collision came from, by its file name -- `input` indexes the dialog's list.
    const auto inputName = [&paths](int input) {
        return input >= 0 && input < static_cast<int>(paths.size()) ? nameOf(paths[static_cast<std::size_t>(input)]) : QString();
    };
    for (const core::FieldCollision& collision : report.fields) {
        lines << (collision.merged() ? catalog::translate(catalog::key::MergeReportMerged).arg(collision.name, inputName(collision.input))
                                     : catalog::translate(catalog::key::MergeReportRenamed).arg(collision.name, inputName(collision.input), collision.renamedTo));
    }
    for (std::size_t input = 0; input < report.references.size(); ++input) {
        const auto cleared = std::count_if(report.references[input].references.begin(), report.references[input].references.end(),
                                           [](const core::PageReference& r) { return r.treatment == core::PageReference::Treatment::Cleared; });
        if (cleared > 0) lines << catalog::translate(catalog::key::MergeReportReferences).arg(inputName(static_cast<int>(input))).arg(static_cast<int>(cleared));
    }
    for (const QString& line : lines) RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "merge report:" << line;
    if (!lines.isEmpty() && aPersonCanRead()) {
        // Non-modal: the report is read beside the new document, never in its way.
        auto* box = new QMessageBox(QMessageBox::Information, catalog::translate(catalog::key::MergeReportTitle), told, QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("mergeReport"));
        box->setDetailedText(lines.join(QLatin1Char('\n')));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setWindowModality(Qt::NonModal);
        box->show();
    }
}

void MainWindow::registerMergeActions()
{
    if (services_.actions.isRegistered(ActionId::MergeDocuments)) return;
    core::ActionRegistry::Registration merge;
    merge.handler = [](KeyContext) {
        MainWindow* window = activeShell();
        if (!window || window->isMerging()) return;
        core::MergeOptions defaults;
        defaults.bookmarks = window->services_.settings.get(core::keys::documents::mergeBookmarkPerFile);
        defaults.renameCollidingFields = window->services_.settings.get(core::keys::documents::renameCollidingFields);
        // The dialog counts each file it lists through the backend -- the read `copyPages` makes, nothing copied.
        core::IDocumentBackend* backend = &window->services_.backend;
        const auto counter = [backend](const QString& path) { return backend->filePageCount(path); };
        if (const std::optional<MergeDialog::Choice> choice = MergeDialog::ask(window, defaults, counter)) window->mergeDocuments(*choice);
    };
    merge.enable = [] {
        const MainWindow* window = activeShell();
        return MergeDialog::canAsk() && !(window && window->isMerging());
    };
    merge.unavailableKey = "shell.tooltip.mergeBusy";
    const core::Result<void> registered = services_.actions.register_(ActionId::MergeDocuments, std::move(merge));
    if (!registered) {
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "merge.documents registration refused --" << registered.error().messageKey;
    }
}

} // namespace rawpage::ui
