// tst_attachmentproduct -- attachments through the product's own composition. Stage 6 T11, decision D6-17, guide
// cases A-85, A-74's attachment clause and A-83 through the pane (`Stage6-Annotations-Testing-Guide.md` §5.14,
// §5.12).
//
// `app/CompositionRoot` and `ui/MainWindow` as `main.cpp` builds them, over the real engine and `corpus/ordinary/
// attachments.pdf`, every command dispatched as the pane's buttons and the tool finder dispatch it. The session
// folder and the shell are the two things a case must not touch for real: the folder is in the case's scratch
// directory (`Options::attachmentRoot`) and the shell is a recorder (`Options::openExternally`), so nothing here
// launches a program.
//
// WHAT IT DOES NOT COVER (convention 19): the two QUESTIONS are answered through
// `MainWindow::setAttachmentAnswersForTesting` -- offscreen there is nobody to ask -- so the dialogs a person sees,
// and Windows handing a file to its program, are M-13's (`Stage6-Annotations-Testing-Guide.md` §6).

#include <catch2/catch_test_macros.hpp>

#include "AnnotProduct.h"
#include "core/cmd/ActionRegistry.h"
#include "core/doc/Attachments.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"
#include "core/text/SearchEngine.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/panes/AttachmentsPane.h"
#include "ui/shell/SidebarDock.h"
#include "ui/write/FindReplaceBar.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTreeWidget>

#include <cmath>
#include <memory>
#include <set>

using namespace rawpage;
using annot_product::Product;

namespace {

// The answers a case gives, put back when it ends -- they are process-wide, and a later case must find none.
struct Answers {
    explicit Answers(ui::MainWindow::AttachmentAnswers answers) { ui::MainWindow::setAttachmentAnswersForTesting(std::move(answers)); }
    ~Answers() { ui::MainWindow::setAttachmentAnswersForTesting({}); }
};

// The product and the shell's recorder; the attachment folder in `outside`, a directory the CASE owns. NOT in the
// product's scratch directory: that is deleted with the product, and a folder deleted with it would pass "emptied at
// the clean quit" whether or not the quit emptied anything -- planted P7 (the quit's emptying removed) stayed green
// until the folder moved here (convention 7).
struct Attaching {
    QStringList handedToShell;
    QString folder;
    std::unique_ptr<Product> product;

    explicit Attaching(const QString& outside = {})
    {
        product = std::make_unique<Product>([this, outside](app::CompositionRoot::Options& options, const QString& scratch) {
            folder = QDir(outside.isEmpty() ? scratch : outside).filePath(QStringLiteral("attachments/session"));
            options.attachmentRoot = folder;
            options.openExternally = [this](const QString& path) {
                handedToShell << path;
                return true;
            };
        });
    }
    Product& p() { return *product; }
    ui::AttachmentsPane& pane() { return *p().window->attachmentsPane(); }
    bool dispatch(core::ActionId id)
    {
        const bool done = static_cast<bool>(p().root->services().actions.dispatch(id, core::KeyContext::Global));
        QCoreApplication::processEvents();
        return done;
    }
    bool selectByName(const QString& name) { return pane().selectByName(name); }
};

QStringList filesUnder(const QString& root)
{
    QStringList out;
    if (!QFileInfo(root).isDir()) return out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next();
    out.sort();
    return out;
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

void sendKey(Product& product, Qt::Key key, Qt::KeyboardModifiers modifiers, const QString& text)
{
    QWidget* target = QApplication::focusWidget();
    if (target == nullptr) target = product.view;
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    QCoreApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
    QCoreApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
}

QPoint widgetAt(const ui::PageView& view, int page, core::PdfPoint point)
{
    const std::optional<core::DevicePoint> d = view.layoutEngine().layout().toContent(page, point);
    REQUIRE(d);
    const core::DeviceRect visible = view.controller().visibleRect();
    const double dpr = view.devicePixelRatioF();
    return QPoint(static_cast<int>(std::lround((d->x - visible.left()) / dpr)),
                  static_cast<int>(std::lround((d->y - visible.top()) / dpr)));
}

void click(ui::PageView& view, QPoint pos)
{
    for (QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, QPointF(pos), view.viewport()->mapToGlobal(QPointF(pos)), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &event);
        QCoreApplication::processEvents();
    }
}

std::string sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

} // namespace

// ==========================================================================================
// A-85 -- opening is asked, and an executable is refused
// ==========================================================================================

TEST_CASE("an attached PDF opens from its bytes a text file is asked first and an executable is refused",
          "[attachments][A-85][D6-17]")
{
    bool asked = false;
    bool yes = false;
    QStringList askedAbout;
    Answers answers(ui::MainWindow::AttachmentAnswers{{}, {}, [&](const QString& name) {
                                                          asked = true;
                                                          askedAbout << name;
                                                          return yes;
                                                      }});
    QTemporaryDir outside;   // outlives the product: see `Attaching`
    REQUIRE(outside.isValid());
    QString folder;
    QStringList shell;
    {
        Attaching a(outside.path());
        folder = a.folder;
        a.p().open("ordinary/attachments.pdf");
        const QStringList scratchBefore = filesUnder(a.p().scratch.path());

        // `Ctrl+Shift+A`'s command: the pane, in front, listing the three files and the pushpin.
        REQUIRE(a.dispatch(core::ActionId::PaneAttachments));
        CHECK(a.p().window->sidebar()->currentPane() == core::ActionId::PaneAttachments);
        REQUIRE(a.pane().rows().size() == 4);
        CHECK(a.pane().rows()[3].page == 0);
        CHECK(a.pane().rows()[3].file.name == QStringLiteral("measurements.csv"));

        // THE PDF: a tab of its own, from its bytes -- no file written anywhere -- read-only, named by the file.
        REQUIRE(a.selectByName(QStringLiteral("appendix.pdf")));
        REQUIRE(a.dispatch(core::ActionId::AttachmentOpen));
        CHECK_FALSE(asked);
        REQUIRE(a.p().window->documentCount() == 2);
        ui::DocumentWindow* copy = a.p().window->currentDocument();
        REQUIRE(copy != nullptr);
        CHECK(copy->isAttachmentCopy());
        CHECK(copy->title() == QStringLiteral("appendix.pdf"));
        CHECK(copy->pageCount() == 1);
        CHECK_FALSE(copy->isEditable());
        CHECK(filesUnder(a.p().scratch.path()) == scratchBefore);
        CHECK_FALSE(QFileInfo::exists(a.folder));
        // A command on it is refused by the backend's key, and nothing changes.
        REQUIRE(copy->journal() != nullptr);
        CHECK_FALSE(copy->journal()->canUndo());

        // Back to the document with the attachments.
        a.p().window->activateDocument(0);
        QCoreApplication::processEvents();

        // THE EXECUTABLE: refused before anything is read or written -- no question, no folder, no shell.
        REQUIRE(a.selectByName(QStringLiteral("setup.exe")));
        CHECK_FALSE(a.p().window->openAttachment(a.pane().current()->ref));
        CHECK_FALSE(asked);
        CHECK_FALSE(QFileInfo::exists(a.folder));
        CHECK(a.handedToShell.isEmpty());

        // THE TEXT FILE: asked first. No, and nothing is written.
        REQUIRE(a.selectByName(QStringLiteral("readme.txt")));
        CHECK_FALSE(a.p().window->openAttachment(a.pane().current()->ref));
        CHECK(askedAbout == QStringList{QStringLiteral("readme.txt")});
        CHECK_FALSE(QFileInfo::exists(a.folder));
        CHECK(a.handedToShell.isEmpty());
        // Yes, and it lands in the session folder -- one file, its bytes the attachment's -- and goes to the shell.
        yes = true;
        REQUIRE(a.p().window->openAttachment(a.pane().current()->ref));
        const QStringList landed = filesUnder(a.folder);
        REQUIRE(landed.size() == 1);
        CHECK(QFileInfo(landed.front()).fileName() == QStringLiteral("readme.txt"));
        CHECK(readAll(landed.front()) == QByteArray("Notes for reviewers: the appendix holds the raw tables.\n"));
        REQUIRE(a.handedToShell.size() == 1);
        CHECK(QFileInfo(a.handedToShell.front()).absoluteFilePath() == QFileInfo(landed.front()).absoluteFilePath());
        shell = a.handedToShell;
        // Opened twice, a second folder: the first copy, which a program may hold, is never met.
        REQUIRE(a.p().window->openAttachment(a.pane().current()->ref));
        CHECK(filesUnder(a.folder).size() == 2);
    }
    // THE CLEAN QUIT -- the product destroyed as `main.cpp` ends -- empties the session folder.
    CHECK(filesUnder(folder).isEmpty());
    CHECK_FALSE(QFileInfo::exists(folder));
    CHECK(shell.size() == 1);
}

TEST_CASE("an attachment copy saved as a file is reopened from it as an ordinary document", "[attachments][A-85][D6-17]")
{
    Attaching a;
    a.p().open("ordinary/attachments.pdf");
    const QString target = QDir(a.p().scratch.path()).filePath(QStringLiteral("appendix-own.pdf"));
    ui::MainWindow::setSaveAsAnswerForTesting([&](const QString&) { return std::optional<QString>(target); });
    REQUIRE(a.dispatch(core::ActionId::PaneAttachments));
    REQUIRE(a.selectByName(QStringLiteral("appendix.pdf")));
    REQUIRE(a.dispatch(core::ActionId::AttachmentOpen));
    REQUIRE(a.p().window->currentDocument()->isAttachmentCopy());
    // Save As is offered for it although it is read-only: its one way to become editable.
    CHECK(a.p().root->services().actions.isEnabled(core::ActionId::FileSaveAs));
    REQUIRE(a.dispatch(core::ActionId::FileSaveAs));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    ui::MainWindow::setSaveAsAnswerForTesting({});

    REQUIRE(a.p().window->documentCount() == 2);
    ui::DocumentWindow* saved = a.p().window->currentDocument();
    REQUIRE(saved != nullptr);
    CHECK_FALSE(saved->isAttachmentCopy());
    CHECK(QFileInfo(saved->path()).absoluteFilePath() == QFileInfo(target).absoluteFilePath());
    CHECK(saved->isEditable());
    // The file is the attachment's bytes, unchanged.
    auto bytes = a.p().root->services().backend.attachmentData(a.p().window->documentAt(0)->handle(),
                                                               core::AttachmentRef::ofDocument(QStringLiteral("appendix.pdf")));
    REQUIRE(bytes);
    CHECK(readAll(target) == bytes.value());
}

// ==========================================================================================
// A-83 through the pane, and annot.attachFile
// ==========================================================================================

TEST_CASE("the pane adds extracts and deletes through commands and the pushpin tool attaches a file where clicked",
          "[attachments][A-83][A-84][D6-17]")
{
    Attaching a;
    a.p().open("ordinary/attachments.pdf");
    const QString source = QDir(a.p().scratch.path()).filePath(QStringLiteral("budget.csv"));
    {
        QFile file(source);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("item,cost\npaper,12\n");
    }
    const QString extracted = QDir(a.p().scratch.path()).filePath(QStringLiteral("out/measurements.csv"));
    Answers answers(ui::MainWindow::AttachmentAnswers{[&] { return std::optional<QString>(source); },
                                                      [&](const QString&) { return std::optional<QString>(extracted); }, {}});
    core::CommandJournal& journal = *a.p().document()->journal();
    REQUIRE(a.dispatch(core::ActionId::PaneAttachments));

    // ADD: one command, listed at once, and its bytes the file's.
    REQUIRE(a.dispatch(core::ActionId::AttachmentAdd));
    CHECK(journal.historySize() == 1);
    REQUIRE(a.pane().rows().size() == 5);
    REQUIRE(a.pane().current());
    CHECK(a.pane().current()->file.name == QStringLiteral("budget.csv"));
    auto bytes = a.p().root->services().backend.attachmentData(a.p().document()->handle(), a.pane().current()->ref);
    REQUIRE(bytes);
    CHECK(bytes.value() == QByteArray("item,cost\npaper,12\n"));

    // EXTRACT the pushpin's file, where the person chose.
    REQUIRE(a.selectByName(QStringLiteral("measurements.csv")));
    REQUIRE(a.dispatch(core::ActionId::AttachmentExtract));
    CHECK(sha256(readAll(extracted)) == "9ff358efdcb7d61c9f4661540bf1e1342c7a9640f8161babfd3e7316afc569d7");

    // DELETE a document file: one command; and the undo puts it back in the list.
    REQUIRE(a.selectByName(QStringLiteral("readme.txt")));
    REQUIRE(a.dispatch(core::ActionId::AttachmentDelete));
    CHECK(journal.historySize() == 2);
    CHECK(a.pane().rows().size() == 4);
    CHECK_FALSE(a.selectByName(QStringLiteral("readme.txt")));
    REQUIRE(journal.undo());
    QCoreApplication::processEvents();
    CHECK(a.selectByName(QStringLiteral("readme.txt")));

    // THE PUSHPIN TOOL: armed, a click on page 1, and the chosen file attached there -- one more command.
    ui::PageView& view = *a.p().view;
    view.revealPageRect(0, core::PdfRect{core::PdfPoint{100, 400}, core::PdfSize{300, 200}});
    a.p().settle();
    REQUIRE(a.p().root->services().actions.dispatch(core::ActionId::AnnotAttachFile, core::KeyContext::Viewer));
    CHECK(view.tools().armedAnnotation() == ui::AnnotationToolKind::AttachFile);
    const auto before = journal.position();   // the undo above left a redo step, which this command drops
    click(view, widgetAt(view, 0, core::PdfPoint{200, 500}));
    CHECK(journal.position() == before + 1);
    CHECK_FALSE(journal.canRedo());
    const core::AnnotationStore& store = journal.document().annotations();
    std::optional<core::Annotation> pin;
    for (const core::Annotation& x : store.all())
        if (x.subtype == core::AnnotationSubtype::FileAttachment && x.attachment && x.attachment->carried) pin = x;
    REQUIRE(pin);
    CHECK(pin->attachment->name == QStringLiteral("budget.csv"));
    CHECK(pin->icon == QStringLiteral("PushPin"));
    CHECK(std::abs(pin->rect.left() + pin->rect.size.width / 2 - 200) < 1.0);   // the pin's point where the click was
    CHECK(std::abs(pin->rect.bottom() - 500) < 1.0);
    // Listed in the pane, with its page.
    CHECK(a.pane().select(core::AttachmentRef::ofAnnotation(pin->id)));

    // AND SAVED: the new file, the pushpin's /FS and the undone delete, all read back from the saved file.
    const QString target = QDir(a.p().scratch.path()).filePath(QStringLiteral("saved.pdf"));
    ui::MainWindow::setSaveAsAnswerForTesting([&](const QString&) { return std::optional<QString>(target); });
    REQUIRE(a.dispatch(core::ActionId::FileSaveAs));
    ui::MainWindow::setSaveAsAnswerForTesting({});
    pdf::DocumentBackendImpl fresh;
    auto reopened = fresh.open(target, nullptr);
    REQUIRE(reopened);
    REQUIRE(fresh.attachments(reopened.value()) != nullptr);
    CHECK(fresh.attachments(reopened.value())->size() == 4);
    auto pinned = fresh.attachmentData(reopened.value(), core::AttachmentRef::ofDocument(QStringLiteral("budget.csv")));
    REQUIRE(pinned);
    CHECK(pinned.value() == QByteArray("item,cost\npaper,12\n"));
}

// ==========================================================================================
// A-74's attachment clause -- a hit in an attachment selects it in the pane
// ==========================================================================================

TEST_CASE("a find hit on an attachment selects it in the Attachments pane and opens nothing", "[attachments][A-74][D6-11]")
{
    Attaching a;
    a.p().open("ordinary/attachments.pdf");
    a.p().root->services().settings.set(core::keys::search::includeAttachments, true);
    ui::FindReplaceBar& bar = *a.p().document()->findBar();
    a.p().view->setFocus(Qt::OtherFocusReason);
    sendKey(a.p(), Qt::Key_F, Qt::ControlModifier, QString());
    REQUIRE(bar.isOpen());
    bar.field()->setFocus(Qt::OtherFocusReason);
    const int finished = bar.searchesFinished();
    for (const QChar ch : QStringLiteral("readme")) sendKey(a.p(), Qt::Key_unknown, Qt::NoModifier, QString(ch));
    QElapsedTimer timer;
    timer.start();
    while ((bar.isSearchPending() || bar.isSearching() || bar.searchesFinished() == finished) && timer.elapsed() < 30000)
        a.p().settle();
    REQUIRE(bar.searchesFinished() > finished);
    a.p().settle();

    const std::vector<core::text::SearchHit> hits = bar.hits();
    REQUIRE_FALSE(hits.empty());
    CHECK(hits.front().contentClass() == core::text::ContentClass::Attachments);
    // The pane in front, the file current -- and no tab opened, nothing written.
    CHECK(a.p().window->sidebar()->currentPane() == core::ActionId::PaneAttachments);
    REQUIRE(a.pane().current());
    CHECK(a.pane().current()->file.name == QStringLiteral("readme.txt"));
    CHECK(a.p().window->documentCount() == 1);
    CHECK_FALSE(QFileInfo::exists(a.folder));
}
