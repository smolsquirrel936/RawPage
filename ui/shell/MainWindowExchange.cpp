// ui/shell/MainWindowExchange -- `MainWindow`'s comments in and out: import, export, migration, and the annotations'
// cut, copy and paste. Stage 6 T13, decisions D6-20, D6-21, D6-22 and D6-40 (`Stage6-Annotations-Plan.md` §3).
//
// A file of its own because `MainWindow.cpp` is the window's frame and this is one feature's surface over it; the
// member functions are `MainWindow`'s, declared in its header. NOTHING HERE DECIDES WHAT AN IMPORT MEANS: the merge is
// `core::AnnotationStore::importFdf`, the migration `core::CommentMigrator`, the files `IDocumentBackend`'s -- this
// asks the person which file, hands the answer down, runs the ONE command that comes back and words what it did.

#include "ui/shell/MainWindow.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/AnnotationGeometry.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/CommentMigrator.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "ui/dialogs/PasswordDialog.h"
#include "ui/panes/CommentsPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/CommentMigration.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/PageView.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace rawpage::ui {

using core::ActionId;
using core::KeyContext;
using core::LogSubsystem;

namespace {

bool canPrompt()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

MainWindow::ExchangeAnswers& installedExchangeAnswers()
{
    static MainWindow::ExchangeAnswers answers;
    return answers;
}

core::Error exchangeError(core::ErrorCode code, const char* key, const QString& context)
{
    return core::Error{code, QLatin1String(key), context};
}

QString typeWord(core::AnnotationSubtype subtype)
{
    if (core::isCommentSubtype(subtype))
        return catalog::translate(QStringLiteral("comments.type.") + QString(core::annotationSubtypeName(subtype)));
    return catalog::translate(catalog::key::CommentsTypeOther);
}

QString outcomeWord(core::MergeEntry::Outcome outcome)
{
    switch (outcome) {
    case core::MergeEntry::Outcome::Added: return catalog::translate(catalog::key::ExchangeOutcomeAdded);
    case core::MergeEntry::Outcome::Replaced: return catalog::translate(catalog::key::ExchangeOutcomeReplaced);
    case core::MergeEntry::Outcome::Kept: return catalog::translate(catalog::key::ExchangeOutcomeKept);
    case core::MergeEntry::Outcome::Refused: return catalog::translate(catalog::key::ExchangeOutcomeRefused);
    }
    return QString();
}

QString reasonOf(const std::optional<core::Error>& why)
{
    return why ? catalog::translateError(QString(why->messageKey), why->context) : QString();
}

// The counts a merge's summary words: added, replaced, kept, not imported.
QString mergeSummary(const core::MergeResult& result)
{
    return catalog::translate(catalog::key::ExchangeImportSummary)
        .arg(result.count(core::MergeEntry::Outcome::Added))
        .arg(result.count(core::MergeEntry::Outcome::Replaced))
        .arg(result.count(core::MergeEntry::Outcome::Kept))
        .arg(result.count(core::MergeEntry::Outcome::Refused));
}

} // namespace

void MainWindow::setExchangeAnswersForTesting(ExchangeAnswers answers)
{
    installedExchangeAnswers() = std::move(answers);
}

CommentMigration* MainWindow::migration() const
{
    return migration_;
}

void MainWindow::showExchangeReport(ExchangeReport report)
{
    lastExchangeReport_ = report;
    // SHOWN WHERE A PERSON CAN SEE IT -- and never offscreen, where it would be a window nobody closes.
    if (!canPrompt()) return;
    auto* dialog = new ExchangeReportDialog(report, this);
    dialog->show();
    dialog->raise();
}

// ==========================================================================================
// Import (D6-20)
// ==========================================================================================

core::Result<core::MergeResult> MainWindow::importComments()
{
    std::optional<QString> path;
    if (installedExchangeAnswers().importPath) {
        path = installedExchangeAnswers().importPath();
    } else if (canPrompt()) {
        const QString chosen = QFileDialog::getOpenFileName(this, catalog::translate(catalog::key::ExchangeImportTitle), QString(),
                                                            catalog::translate(catalog::key::ExchangeFileFilter));
        if (!chosen.isEmpty()) path = chosen;
    }
    if (!path) return exchangeError(core::ErrorCode::Cancelled, "error.exchange.cancelled", QString());
    return importCommentsFrom(*path);
}

core::Result<core::MergeResult> MainWindow::importCommentsFrom(const QString& path)
{
    DocumentWindow* document = currentDocument();
    core::CommandJournal* journal = document ? document->journal() : nullptr;
    const auto refuse = [this](const core::Error& error) {
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return error;
    };
    if (journal == nullptr) return refuse(exchangeError(core::ErrorCode::Unavailable, "error.edit.notAssessed", QString()));

    core::Result<core::Fdf> read = services_.backend.readAnnotationExchange(path);
    if (!read) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "import: refused" << path << "--" << read.error().messageKey;
        return refuse(read.error());
    }
    const core::Document& model = journal->document();
    const core::FdfImport plan = model.annotations().importFdf(
        read.value(), core::importCheck(model, [this](const core::Annotation& a) { return services_.backend.annotationWritable(a); }));
    if (!plan.changesNothing()) {
        if (const core::Status ran = journal->run(std::make_unique<core::ImportAnnotationsCommand>(plan.added, plan.replaced)); !ran)
            return refuse(ran.error());
    }
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "import:" << path << "--" << plan.result.count(core::MergeEntry::Outcome::Added) << "added,"
                                       << plan.result.count(core::MergeEntry::Outcome::Replaced) << "replaced,"
                                       << plan.result.count(core::MergeEntry::Outcome::Kept) << "kept,"
                                       << plan.result.count(core::MergeEntry::Outcome::Refused) << "refused,"
                                       << static_cast<int>(plan.result.notes.size()) << "note(s)";
    const QString summary = mergeSummary(plan.result);
    if (status_) status_->setMessage(summary);

    ExchangeReport report;
    report.title = catalog::translate(catalog::key::ExchangeImportTitle);
    report.summary = summary;
    for (const core::MergeEntry& e : plan.result.entries)
        report.rows.push_back(ExchangeReport::Row{e.name, typeWord(e.subtype), e.page, outcomeWord(e.outcome), reasonOf(e.why), {}});
    // THE REPORT IS SHOWN WHEN SOMETHING WAS NOT BROUGHT OVER: a refusal must be read by name (D6-40).
    if (plan.result.count(core::MergeEntry::Outcome::Refused) > 0) showExchangeReport(std::move(report));
    else lastExchangeReport_ = std::move(report);
    return plan.result;
}

// ==========================================================================================
// Export (D6-20)
// ==========================================================================================

core::Status MainWindow::exportComments()
{
    DocumentWindow* document = currentDocument();
    if (document == nullptr) return exchangeError(core::ErrorCode::Unavailable, "error.edit.notAssessed", QString());
    const QString source = !document->path().isEmpty() ? document->path() : document->sourcePath();
    const QString folder = source.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                            : QFileInfo(source).absolutePath();
    const QString base = source.isEmpty() ? QStringLiteral("comments") : QFileInfo(source).completeBaseName();
    const QString proposed = QDir(folder).filePath(base + QStringLiteral(".xfdf"));
    std::optional<QString> target;
    if (installedExchangeAnswers().exportPath) {
        target = installedExchangeAnswers().exportPath(proposed);
    } else if (canPrompt()) {
        QString filter;
        QString chosen = QFileDialog::getSaveFileName(this, catalog::translate(catalog::key::ExchangeExportTitle), proposed,
                                                      catalog::translate(catalog::key::ExchangeFileFilter), &filter);
        if (!chosen.isEmpty() && !core::exchangeFormatOfPath(chosen))
            chosen += filter.contains(QLatin1String("*.fdf")) && !filter.contains(QLatin1String("*.xfdf")) ? QStringLiteral(".fdf")
                                                                                                          : QStringLiteral(".xfdf");
        if (!chosen.isEmpty()) target = chosen;
    }
    if (!target) return exchangeError(core::ErrorCode::Cancelled, "error.exchange.cancelled", QString());
    return exportCommentsTo(*target);
}

core::Status MainWindow::exportCommentsTo(const QString& path)
{
    DocumentWindow* document = currentDocument();
    const core::AnnotationStore* store = document ? services_.backend.annotations(document->handle()) : nullptr;
    core::CommandJournal* journal = document ? document->journal() : nullptr;
    // THE DOCUMENT AS IT STANDS: the journal's model where there is one -- an unsaved comment is exported -- else what
    // the backend read.
    if (journal != nullptr) store = &journal->document().annotations();
    const auto refuse = [this](const core::Error& error) {
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return error;
    };
    if (store == nullptr || !store->status())
        return refuse(store ? store->status().error() : exchangeError(core::ErrorCode::Unavailable, "error.edit.notAssessed", QString()));
    const std::optional<core::ExchangeFormat> format = core::exchangeFormatOfPath(path);
    if (!format) return refuse(exchangeError(core::ErrorCode::InvalidArgument, "error.exchange.unknownFormat", QFileInfo(path).fileName()));

    // WHAT THE PANE LISTS (D6-20): every comment, unless it is filtered -- by identity, back to the store's indices.
    std::vector<std::size_t> items;
    if (comments_ != nullptr && comments_->store() == store) {
        for (const core::AnnotationId& id : comments_->listedComments()) {
            if (const std::optional<std::size_t> index = store->find(id)) items.push_back(*index);
        }
    } else {
        items = store->topLevel();
    }
    core::Fdf exchange = store->exportFdf(items);
    if (exchange.annotations.empty()) return refuse(exchangeError(core::ErrorCode::NotFound, "error.exchange.nothingToExport", QString()));
    const QString source = !document->path().isEmpty() ? document->path() : document->sourcePath();
    exchange.file = QFileInfo(source).fileName();
    // The save dialog asked about a file of that name already, so the export replaces it.
    if (const core::Status written = services_.backend.writeAnnotationExchange(document->handle(), exchange, *format, path, true); !written)
        return refuse(written.error());
    int comments = 0;
    for (const core::Annotation& a : exchange.annotations) {
        if (a.subtype != core::AnnotationSubtype::Popup) ++comments;
    }
    if (status_) status_->setMessage(catalog::translate(catalog::key::ExchangeExported).arg(comments).arg(QDir::toNativeSeparators(path)));
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "export:" << comments << "comment(s) to" << path;
    return {};
}

// ==========================================================================================
// Migration (D6-21)
// ==========================================================================================

core::Status MainWindow::migrateComments()
{
    std::optional<QString> path;
    if (installedExchangeAnswers().migratePath) {
        path = installedExchangeAnswers().migratePath();
    } else if (canPrompt()) {
        const QString chosen = QFileDialog::getOpenFileName(this, catalog::translate(catalog::key::ExchangeMigrateTitle), QString(),
                                                            catalog::translate(catalog::key::ExchangePdfFilter));
        if (!chosen.isEmpty()) path = chosen;
    }
    if (!path) return exchangeError(core::ErrorCode::Cancelled, "error.exchange.cancelled", QString());
    return migrateCommentsFrom(*path);
}

core::Status MainWindow::migrateCommentsFrom(const QString& olderPath)
{
    DocumentWindow* document = currentDocument();
    const auto refuse = [this](const core::Error& error) {
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return error;
    };
    if (document == nullptr || document->journal() == nullptr || !document->isEditable())
        return refuse(exchangeError(core::ErrorCode::Unavailable, "error.edit.notAssessed", QString()));
    if (migration_ != nullptr) return refuse(exchangeError(core::ErrorCode::Unavailable, "error.migrate.busy", QString()));
    const QFileInfo info(olderPath);
    if (!info.exists() || !info.isFile()) return refuse(exchangeError(core::ErrorCode::NotFound, "error.open.notFound", olderPath));
    // THE OLDER REVISION OPENED FOR THE MIGRATION -- a handle of its own, never a tab's (the comparison's rule, D5-18).
    const core::Result<core::DocumentHandle> opened =
        services_.backend.open(info.absoluteFilePath(), PasswordDialog::callbackFor(info.fileName(), this));
    if (!opened) return refuse(opened.error());

    migration_ = new CommentMigration(services_, document->handle(), opened.value(), info.absoluteFilePath(), this);
    migrationTarget_ = document;
    connect(migration_, &CommentMigration::progress, this, [this](int done, int total, bool comparing) {
        if (status_)
            status_->setMessage(catalog::translate(comparing ? catalog::key::ExchangeMigrateComparing : catalog::key::ExchangeMigrateReading)
                                    .arg(done)
                                    .arg(total));
    });
    connect(migration_, &CommentMigration::finished, this, &MainWindow::onMigrationFinished);
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "migrate: from" << info.absoluteFilePath() << "into" << document->path();
    migration_->start();
    return {};
}

void MainWindow::onMigrationFinished()
{
    CommentMigration* run = migration_;
    if (run == nullptr) return;
    migration_ = nullptr;
    // RETIRED, NOT DELETED: this runs inside the run's own delivery, and its thread is joined when it goes.
    run->deleteLater();
    const auto done = [this] { emit migrationFinished(); };

    DocumentWindow* document = migrationTarget_;
    core::CommandJournal* journal = document ? document->journal() : nullptr;
    const std::optional<core::Result<core::MigrationResult>>& outcome = run->outcome();
    if (journal == nullptr || !outcome || !*outcome) {
        const core::Error why = outcome && !*outcome ? outcome->error()
                                                      : exchangeError(core::ErrorCode::Unavailable, "error.migrate.targetClosed", QString());
        if (status_) status_->setMessage(catalog::translateError(QString(why.messageKey), why.context));
        done();
        return;
    }
    const core::MigrationResult& migrated = outcome->value();
    const core::Document& model = journal->document();
    const core::FdfImport plan = model.annotations().importFdf(
        migrated.placed, core::importCheck(model, [this](const core::Annotation& a) { return services_.backend.annotationWritable(a); }));
    if (!plan.changesNothing()) {
        if (const core::Status ran = journal->run(std::make_unique<core::ImportAnnotationsCommand>(plan.added, plan.replaced)); !ran) {
            if (status_) status_->setMessage(catalog::translateError(QString(ran.error().messageKey), ran.error().context));
            done();
            return;
        }
    }

    // THE REPORT: every comment of the original, placed (and what the merge made of it) or reported, and why.
    std::map<QString, const core::MergeEntry*> merged;
    for (const core::MergeEntry& e : plan.result.entries) merged.emplace(e.name, &e);
    ExchangeReport report;
    report.title = catalog::translate(catalog::key::ExchangeMigrateTitle);
    int placed = 0;
    int reported = 0;
    for (const core::MigrationEntry& e : migrated.entries) {
        ExchangeReport::Row row{e.name, typeWord(e.subtype), e.page, {}, {}, e.text};
        if (e.outcome == core::MigrationEntry::Outcome::Reported) {
            ++reported;
            row.outcome = catalog::translate(catalog::key::ExchangeOutcomeReported);
            row.reason = reasonOf(e.why);
        } else if (const auto m = merged.find(e.name); m != merged.end()) {
            row.outcome = outcomeWord(m->second->outcome);
            row.reason = reasonOf(m->second->why);
            if (m->second->outcome == core::MergeEntry::Outcome::Refused) ++reported;
            else ++placed;
        }
        report.rows.push_back(std::move(row));
    }
    report.summary = catalog::translate(catalog::key::ExchangeMigrateSummary).arg(placed).arg(migrated.entries.size()).arg(reported);
    if (status_) status_->setMessage(report.summary);
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "migrate:" << placed << "of" << static_cast<int>(migrated.entries.size())
                                       << "comment(s) placed," << reported << "reported";
    showExchangeReport(std::move(report));
    done();
}

// ==========================================================================================
// Cut, copy and paste (D6-22)
// ==========================================================================================

core::Status MainWindow::copyAnnotations()
{
    DocumentWindow* document = currentDocument();
    AnnotationOverlay* overlay = document ? document->annotations() : nullptr;
    const core::AnnotationStore* store = overlay ? overlay->store() : nullptr;
    if (overlay == nullptr || store == nullptr || !overlay->hasSelection())
        return exchangeError(core::ErrorCode::Unavailable, "error.clipboard.nothingSelected", QString());

    // THE SELECTION AND ITS POP-UPS, as the store holds them -- the threads are not copied: a reply copied without its
    // comment would answer nothing on the page it lands on.
    std::vector<core::Annotation> copied;
    std::set<core::AnnotationId> seen;
    for (const core::Annotation& a : overlay->selected()) {
        if (!seen.insert(a.id).second) continue;
        copied.push_back(a);
        if (a.popup) {
            if (const std::optional<std::size_t> popup = store->find(*a.popup); popup && seen.insert(*a.popup).second)
                copied.push_back(store->at(*popup));
        }
    }
    QJsonArray annotations;
    QJsonObject pages;
    QStringList words;
    const core::PageTree* tree = services_.backend.pages(document->handle());
    for (const core::Annotation& a : copied) {
        annotations.append(a.toJson());
        if (a.subtype != core::AnnotationSubtype::Popup && !a.contents.isEmpty()) words << a.contents;
        if (const core::Page* page = tree != nullptr ? tree->at(a.page) : nullptr) {
            const core::PdfRect box = page->cropBox();
            pages.insert(QString::number(a.page), QJsonArray{box.size.width, box.size.height});
        }
    }
    const QJsonObject payload{{QStringLiteral("version"), 1}, {QStringLiteral("pages"), pages}, {QStringLiteral("annotations"), annotations}};
    auto* mime = new QMimeData;
    mime->setData(QLatin1String(kAnnotationsMimeType), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    mime->setText(words.join(QLatin1Char('\n')));
    QGuiApplication::clipboard()->setMimeData(mime);
    if (status_) status_->setMessage(catalog::translate(catalog::key::ExchangeCopied).arg(copied.size()));
    return {};
}

core::Status MainWindow::cutAnnotations()
{
    DocumentWindow* document = currentDocument();
    AnnotationOverlay* overlay = document ? document->annotations() : nullptr;
    if (overlay == nullptr || !overlay->hasSelection())
        return exchangeError(core::ErrorCode::Unavailable, "error.clipboard.nothingSelected", QString());
    if (const core::Status copied = copyAnnotations(); !copied) return copied;
    // A CUT IS A COPY AND ONE DELETE (D6-22) -- the overlay's, which takes each pop-up with its comment and honours
    // D6-27: a locked comment is copied and not cut, and the refusal said.
    const core::Status deleted = overlay->deleteSelection();
    if (!deleted && status_)
        status_->setMessage(catalog::translate(catalog::key::StatusAnnotationRefused)
                                .arg(catalog::translateError(QString(deleted.error().messageKey), deleted.error().context)));
    return deleted;
}

core::Status MainWindow::pasteAnnotations(const QByteArray& payload)
{
    DocumentWindow* document = currentDocument();
    PageView* view = currentView();
    AnnotationOverlay* overlay = document ? document->annotations() : nullptr;
    core::CommandJournal* journal = document ? document->journal() : nullptr;
    if (view == nullptr || overlay == nullptr || journal == nullptr)
        return exchangeError(core::ErrorCode::Unavailable, "error.edit.notAssessed", QString());

    // SOMEONE ELSE'S BYTES, maybe another process's: parsed and bounded as a journal line is.
    const QJsonDocument parsed = QJsonDocument::fromJson(payload);
    const QJsonObject root = parsed.object();
    const QJsonArray list = root.value(QStringLiteral("annotations")).toArray();
    if (!parsed.isObject() || root.value(QStringLiteral("version")).toInt() != 1 || list.isEmpty()
        || static_cast<std::size_t>(list.size()) > core::AnnotationCommandLimits::kMaxAnnotations)
        return exchangeError(core::ErrorCode::ParseFailure, "error.clipboard.malformed", QString());
    std::vector<core::Annotation> copied;
    for (const QJsonValue item : list) {
        core::Result<core::Annotation> a = core::Annotation::fromJson(item.toObject());
        if (!a) return exchangeError(core::ErrorCode::ParseFailure, "error.clipboard.malformed", QString());
        copied.push_back(std::move(a).value());
    }

    // WHERE: the page in front -- the one under the middle of what is shown -- at the copied place when that page is the
    // size the copied page was, else round the middle of what is shown.
    const PagePoint centre = view->pagePointAt(view->viewport()->rect().center());
    if (!centre.isValid()) return exchangeError(core::ErrorCode::Unavailable, "error.edit.notAssessed", QString());
    const core::PageTree* tree = services_.backend.pages(document->handle());
    const core::Page* target = tree != nullptr ? tree->at(centre.page) : nullptr;
    const QJsonArray sourceSize = root.value(QStringLiteral("pages")).toObject().value(QString::number(copied.front().page)).toArray();
    const bool sameSize = target != nullptr && sourceSize.size() == 2
                          && std::fabs(sourceSize[0].toDouble() - target->cropBox().size.width) < 0.01
                          && std::fabs(sourceSize[1].toDouble() - target->cropBox().size.height) < 0.01;
    double dx = 0.0;
    double dy = 0.0;
    if (!sameSize) {
        double left = 0, bottom = 0, right = 0, top = 0;
        bool any = false;
        for (const core::Annotation& a : copied) {
            if (a.subtype == core::AnnotationSubtype::Popup) continue;
            left = any ? std::min(left, a.rect.left()) : a.rect.left();
            bottom = any ? std::min(bottom, a.rect.bottom()) : a.rect.bottom();
            right = any ? std::max(right, a.rect.right()) : a.rect.right();
            top = any ? std::max(top, a.rect.top()) : a.rect.top();
            any = true;
        }
        dx = centre.point.x - (left + right) / 2.0;
        dy = centre.point.y - (bottom + top) / 2.0;
    }

    // EACH A NEW `/NM`, EVERYTHING ELSE AS COPIED (A-81); a reference within the set follows it, one outside it goes.
    std::map<QString, QString> renamed;
    for (const core::Annotation& a : copied) renamed.emplace(a.id.toString(), core::newAnnotationName());
    const auto follow = [&renamed](std::optional<core::AnnotationId>& reference) {
        if (!reference) return;
        const auto found = renamed.find(reference->toString());
        if (found == renamed.end()) reference.reset();
        else reference = core::AnnotationId::ofCreated(found->second);
    };
    const core::ImportCheck check =
        core::importCheck(journal->document(), [this](const core::Annotation& a) { return services_.backend.annotationWritable(a); });
    std::vector<core::Annotation> pasted;
    std::optional<core::Error> firstRefusal;
    for (core::Annotation a : copied) {
        const QString name = renamed.at(a.id.toString());
        if (dx != 0.0 || dy != 0.0) a = core::translated(a, dx, dy);
        a.id = core::AnnotationId::ofCreated(name);
        a.nm = name;
        a.page = centre.page;
        follow(a.inReplyTo);
        follow(a.popup);
        follow(a.parent);
        a.hasAppearance = a.subtype != core::AnnotationSubtype::Popup && !a.isStateReply();
        if (const core::Status ok = check(a); !ok) {
            if (!firstRefusal) firstRefusal = ok.error();
            continue;
        }
        pasted.push_back(std::move(a));
    }
    if (pasted.empty()) return firstRefusal ? *firstRefusal : exchangeError(core::ErrorCode::Unavailable, "error.clipboard.nothingPastable", QString());
    // A pop-up whose comment was refused would be a window onto nothing.
    std::set<QString> kept;
    for (const core::Annotation& a : pasted) kept.insert(a.nm);
    pasted.erase(std::remove_if(pasted.begin(), pasted.end(),
                                [&kept](const core::Annotation& a) {
                                    return a.subtype == core::AnnotationSubtype::Popup && (!a.parent || kept.count(a.parent->nm) == 0);
                                }),
                 pasted.end());
    std::vector<core::AnnotationId> selected;
    for (const core::Annotation& a : pasted) {
        if (a.subtype != core::AnnotationSubtype::Popup) selected.push_back(a.id);
    }
    if (const core::Status added = overlay->addMade(pasted); !added) return added;
    overlay->select(selected);
    if (status_) status_->setMessage(catalog::translate(catalog::key::ExchangePasted).arg(selected.size()));
    return {};
}

// ==========================================================================================
// The four ids
// ==========================================================================================

void MainWindow::registerExchangeActions()
{
    // `comment.import` and `comment.export`, declared at Stage 1; `comment.migrate`, MINTED chordless (D6-21); and
    // `edit.cut`, §8's Common `Ctrl+X` -- annotations cut (D6-22), every text widget keeping its own through
    // `ShortcutBinder`'s handshake. Every handler resolves the window at dispatch time (convention 14); a command that
    // needs a person to name a file is DISABLED where nobody can be asked (convention 16).
    const auto registerOne = [this](ActionId id, core::ActionRegistry::Handler handler, core::ActionRegistry::Enable enable,
                                    const char* unavailable) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = std::move(handler);
        registration.enable = std::move(enable);
        if (unavailable != nullptr) registration.unavailableKey = unavailable;
        services_.actions.register_(id, std::move(registration));
    };
    const auto editable = [] {
        MainWindow* window = activeShell();
        const DocumentWindow* document = window ? window->currentDocument() : nullptr;
        return document != nullptr && document->isEditable();
    };
    const auto hasComments = [] {
        MainWindow* window = activeShell();
        const DocumentWindow* document = window ? window->currentDocument() : nullptr;
        const core::CommandJournal* journal = document ? document->journal() : nullptr;
        const core::AnnotationStore* store = journal ? &journal->document().annotations()
                                             : (document ? window->services_.backend.annotations(document->handle()) : nullptr);
        if (store == nullptr || !store->status()) return false;
        for (const std::size_t index : store->topLevel()) {
            if (core::isCommentSubtype(store->at(index).subtype)) return true;
        }
        return false;
    };
    const auto annotationsSelected = [] {
        MainWindow* window = activeShell();
        const DocumentWindow* document = window ? window->currentDocument() : nullptr;
        const AnnotationOverlay* overlay = document ? document->annotations() : nullptr;
        return overlay != nullptr && overlay->hasSelection() && document->isEditable();
    };
    const auto canAsk = [](auto member) { return static_cast<bool>(installedExchangeAnswers().*member) || canPrompt(); };

    registerOne(ActionId::CommentImport,
                [](KeyContext) {
                    if (MainWindow* window = activeShell()) (void)window->importComments();
                },
                [editable, canAsk] { return editable() && canAsk(&ExchangeAnswers::importPath); }, "shell.tooltip.needsDocument");
    registerOne(ActionId::CommentExport,
                [](KeyContext) {
                    if (MainWindow* window = activeShell()) (void)window->exportComments();
                },
                [hasComments, canAsk] { return hasComments() && canAsk(&ExchangeAnswers::exportPath); }, "shell.tooltip.needsComments");
    registerOne(ActionId::CommentMigrate,
                [](KeyContext) {
                    if (MainWindow* window = activeShell()) (void)window->migrateComments();
                },
                [editable, canAsk] {
                    MainWindow* window = activeShell();
                    return editable() && canAsk(&ExchangeAnswers::migratePath) && window != nullptr && window->migration_ == nullptr;
                },
                "shell.tooltip.needsDocument");
    registerOne(ActionId::EditCut,
                [](KeyContext) {
                    if (MainWindow* window = activeShell()) (void)window->cutAnnotations();
                },
                annotationsSelected, "shell.tooltip.needsSelection");
}

} // namespace rawpage::ui
