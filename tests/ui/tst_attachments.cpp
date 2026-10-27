// tst_attachments (ui.) -- the Attachments pane over the fake backend. Stage 6 T11, decision D6-17
// (`Stage6-Annotations-Testing-Guide.md` §5.14).
//
// What the widgets decide: `Ctrl+Shift+A` through the shortcut binder, the pane's list of both kinds of attachment,
// a pane that never widens its window (found at T11 by `tst_restore`, below) and whose four buttons are each as wide as
// their word (S6 guide I-50, found in M-13's walk), a command that needs a person to name
// a file DISABLED where nobody can be asked (convention 16), and a pushpin deleted with its pop-up. WHAT IT DOES NOT
// COVER (convention 19): the fake backend reads no file and writes what it is told -- the bytes, the open and the
// session folder are `tests/composition/tst_attachmentproduct.cpp`'s, over the real engine.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Attachments.h"
#include "core/doc/Document.h"
#include "tests/ui/ShellFixture.h"
#include "ui/panes/AttachmentsPane.h"
#include "ui/panes/CommentsPane.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/SidebarDock.h"
#include "ui/viewer/PageView.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLayout>
#include <QToolButton>
#include <QTreeWidget>

#include <memory>

using namespace rawpage;
using core::ActionId;

namespace {

core::DocumentAttachment file(const char* name, const char* description, const char* bytes)
{
    core::DocumentAttachment out{QString::fromLatin1(name),
                                 core::newEmbeddedFile(QString::fromLatin1(name), QByteArray(bytes), QString::fromLatin1(description), 1791504000)};
    out.file.carried = false;   // as a reading gives it: what the file says, never its bytes
    out.file.data.clear();
    return out;
}

// A pushpin on page 2 and its pop-up, as a file holds them.
core::AnnotationStore pinnedStore()
{
    core::AnnotationStore::Reading reading;
    reading.pages.resize(2);
    core::Annotation pin;
    pin.id = core::AnnotationId::ofObject(40);
    pin.subtype = core::AnnotationSubtype::FileAttachment;
    pin.page = 1;
    pin.rect = core::PdfRect{core::PdfPoint{100, 600}, core::PdfSize{16, 24}};
    pin.icon = QStringLiteral("PushPin");
    pin.hasAppearance = true;
    pin.popup = core::AnnotationId::ofObject(41);
    core::EmbeddedFile carried = core::newEmbeddedFile(QStringLiteral("data.csv"), QByteArray("a,b\n"), QString(), 1791504000);
    carried.carried = false;
    carried.data.clear();
    pin.attachment = carried;
    core::Annotation popup;
    popup.id = core::AnnotationId::ofObject(41);
    popup.subtype = core::AnnotationSubtype::Popup;
    popup.page = 1;
    popup.rect = core::PdfRect{core::PdfPoint{120, 500}, core::PdfSize{180, 100}};
    popup.parent = pin.id;
    reading.annotations = {pin, popup};
    for (std::size_t at = 0; at < reading.annotations.size(); ++at) {
        core::AnnotationListingEntry entry;
        entry.annotation = at;
        entry.fact.dictionary = true;
        entry.fact.subtype = reading.annotations[at].subtype;
        entry.fact.rect = reading.annotations[at].rect;
        reading.pages[1].push_back(entry);
    }
    return core::AnnotationStore::fromReading(std::move(reading));
}

struct Fixture : uitest::ShellFixture {
    QString writeFile(const QString& name)
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile out(path);
        REQUIRE(out.open(QIODevice::WriteOnly));
        out.write("the fake backend decides, not these bytes");
        out.close();
        return QFileInfo(path).absoluteFilePath();
    }
};

struct Attached {
    Fixture fixture;
    std::unique_ptr<ui::MainWindow> window;

    explicit Attached(bool editable = true)
    {
        fixture.backend.pagesPerDocument = 2;
        fixture.backend.editable = editable;
        fixture.backend.attachments_ = core::Attachments::fromReading(
            {file("appendix.pdf", "An appendix", "%PDF-1.7"), file("readme.txt", "Notes", "notes\n")});
        fixture.backend.annotations_ = pinnedStore();
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1000, 800);
        window->openFiles({fixture.writeFile(QStringLiteral("attached.pdf"))});
        window->show();
        layout();
        window->raiseToForeground();
        REQUIRE(window->currentView() != nullptr);
        window->currentView()->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
    }
    ~Attached() { ui::MainWindow::setAttachmentAnswersForTesting({}); }

    void layout() const
    {
        for (QWidget* widget : window->findChildren<QWidget*>())
            if (QLayout* l = widget->layout()) l->activate();
        QCoreApplication::processEvents();
    }
    ui::AttachmentsPane& pane() const { return *window->attachmentsPane(); }
    bool key(Qt::Key key, Qt::KeyboardModifiers modifiers) const
    {
        QWidget* focus = QApplication::focusWidget();
        REQUIRE(focus != nullptr);
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        const bool taken = QApplication::sendEvent(focus, &press) && press.isAccepted();
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QApplication::sendEvent(focus, &release);
        QCoreApplication::processEvents();
        return taken;
    }
};

} // namespace

TEST_CASE("Ctrl+Shift+A shows the Attachments pane listing the document's files and its pushpins", "[attachments][D6-17]")
{
    Attached a;
    CHECK(a.window->sidebar()->currentPane() != ActionId::PaneAttachments);
    CHECK(a.key(Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier));
    CHECK(a.window->sidebar()->currentPane() == ActionId::PaneAttachments);

    REQUIRE(a.pane().rows().size() == 3);
    CHECK(a.pane().rows()[0].file.name == QStringLiteral("appendix.pdf"));
    CHECK(a.pane().rows()[0].page == -1);
    CHECK(a.pane().rows()[1].file.name == QStringLiteral("readme.txt"));
    CHECK(a.pane().rows()[2].file.name == QStringLiteral("data.csv"));
    CHECK(a.pane().rows()[2].page == 1);
    const QTreeWidget* list = a.pane().view();
    REQUIRE(list->topLevelItemCount() == 3);
    CHECK(list->topLevelItem(0)->text(1) == QStringLiteral("An appendix"));
    CHECK(list->topLevelItem(2)->text(4).contains(QLatin1Char('2')));   // "Page 2"

    // With no tab in front, the pane says so and lists nothing.
    REQUIRE(a.window->closeDocument(0));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    CHECK(a.pane().rows().empty());
}

TEST_CASE("the Attachments pane never widens its window", "[attachments][D6-17]")
{
    // FOUND BY `tst_restore` AT T11: the pane's four buttons, each its command's full label, side by side, held the
    // sidebar -- and so the window -- open to 1892 pixels when 1400 were asked for. The buttons say one word and give
    // way; the pane is no wider than the Comments pane beside it in the same dock.
    Attached a;
    REQUIRE(a.window->attachmentsPane() != nullptr);
    REQUIRE(a.window->commentsPane() != nullptr);
    const int attachments = a.pane().minimumSizeHint().width();
    const int comments = a.window->commentsPane()->minimumSizeHint().width();
    INFO("attachments " << attachments << ", comments " << comments);
    CHECK(attachments <= comments);
    // And through the window: the pane shown, the window keeps the width it was given -- 1400, above the shell's own
    // floor (the ribbon's minimum, about 1300 offscreen; `tst_restore` says why a narrower one proves nothing).
    a.window->resize(1400, 800);
    CHECK(a.key(Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier));
    a.layout();
    CHECK(a.window->sidebar()->currentPane() == ActionId::PaneAttachments);
    CHECK(a.window->width() == 1400);
}

TEST_CASE("the Attachments pane's four buttons are each as wide as the word they say", "[attachments][D6-17][I-50]")
{
    // S6 GUIDE I-50, FOUND IN M-13'S WALK: the case above made the buttons give way -- each IGNORED its width, beside a
    // stretch -- and a layout that hands the spare width by stretch gave all of it to the stretch and none to them. Four
    // buttons ZERO pixels wide, at every width of the pane, while every case reached their commands without a click: a
    // pane that is narrow enough was the only thing asked. This asks the other half (convention 22), at the sidebar's
    // width in two windows, after the layout has run (convention 12).
    Attached a;
    CHECK(a.key(Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier));
    for (const int width : {1000, 1400}) {
        a.window->resize(width, 800);
        a.layout();
        REQUIRE(a.window->sidebar()->currentPane() == ActionId::PaneAttachments);
        REQUIRE(a.pane().isVisible());
        const QList<QToolButton*> buttons = a.pane().findChildren<QToolButton*>();
        REQUIRE(buttons.size() == 4);
        for (const QToolButton* b : buttons) {
            const int word = b->fontMetrics().horizontalAdvance(b->text());
            INFO("window " << width << ", pane " << a.pane().width() << ": '" << b->text().toStdString() << "' "
                           << b->width() << " px wide for a word of " << word);
            CHECK(b->isVisible());
            CHECK(b->width() >= word);
            CHECK(a.pane().rect().contains(b->geometry()));
        }
    }
}

TEST_CASE("an attachment command that needs a person to name a file is disabled where nobody can be asked",
          "[attachments][D6-17][convention-16]")
{
    Attached a;
    CHECK(a.key(Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier));
    REQUIRE(a.pane().select(a.pane().rows()[1].ref));
    core::ActionRegistry& actions = a.fixture.actions;
    // Offscreen, with nobody to ask and no answers: the add and the extract are disabled; the open and the delete,
    // which name no file, are not.
    CHECK_FALSE(actions.isEnabled(ActionId::AttachmentAdd));
    CHECK_FALSE(actions.isEnabled(ActionId::AttachmentExtract));
    CHECK(actions.isEnabled(ActionId::AttachmentOpen));
    CHECK(actions.isEnabled(ActionId::AttachmentDelete));
    // With answers, they are enabled.
    ui::MainWindow::setAttachmentAnswersForTesting(
        {[] { return std::optional<QString>(); }, [](const QString&) { return std::optional<QString>(); }, {}});
    CHECK(actions.isEnabled(ActionId::AttachmentAdd));
    CHECK(actions.isEnabled(ActionId::AttachmentExtract));
    // And on a document this build will not edit, nothing that changes it is enabled.
    Attached readOnly(false);
    CHECK(readOnly.key(Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier));
    REQUIRE(readOnly.pane().select(readOnly.pane().rows()[0].ref));
    CHECK_FALSE(readOnly.fixture.actions.isEnabled(ActionId::AttachmentAdd));
    CHECK_FALSE(readOnly.fixture.actions.isEnabled(ActionId::AttachmentDelete));
    CHECK(readOnly.fixture.actions.isEnabled(ActionId::AttachmentOpen));
}

TEST_CASE("a file deleted from the pane is one command and a pushpin goes with its pop-up", "[attachments][A-83][D6-17]")
{
    Attached a;
    CHECK(a.key(Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier));
    core::CommandJournal& journal = *a.window->currentDocument()->journal();

    REQUIRE(a.pane().selectByName(QStringLiteral("readme.txt")));
    REQUIRE(a.fixture.actions.dispatch(ActionId::AttachmentDelete, core::KeyContext::Global));
    CHECK(journal.position() == 1);
    CHECK(a.pane().rows().size() == 2);
    CHECK_FALSE(a.pane().selectByName(QStringLiteral("readme.txt")));

    REQUIRE(a.pane().selectByName(QStringLiteral("data.csv")));
    REQUIRE(a.fixture.actions.dispatch(ActionId::AttachmentDelete, core::KeyContext::Global));
    CHECK(journal.position() == 2);
    CHECK(a.pane().rows().size() == 1);
    const core::AnnotationStore& store = journal.document().annotations();
    CHECK(store.placementsOf(core::AnnotationId::ofObject(40)).empty());
    CHECK(store.placementsOf(core::AnnotationId::ofObject(41)).empty());   // the pop-up with it

    REQUIRE(journal.undo());
    REQUIRE(journal.undo());
    QCoreApplication::processEvents();
    CHECK(a.pane().rows().size() == 3);
}
