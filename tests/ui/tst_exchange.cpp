// tst_exchange -- Stage 6 T13 through the widgets: annotations cut, copied and pasted, comments imported and exported,
// and the three file questions (`Stage6-Annotations-Testing-Guide.md` §5.13's A-81 and §5.16; D6-20, D6-21, D6-22).
//
//   * A-81: `Ctrl+C` on the page with annotations selected puts THEM on the clipboard, under RawPage's own type and
//     as their words; `Ctrl+V` in another tab adds them -- each a new `/NM`, every other field as copied, a note's
//     pop-up with it -- as ONE command; `Ctrl+X` is the copy and ONE delete; the bytes alone, as a second process
//     would have them, paste the same; and the find bar, the properties bar's text field and the tool finder each keep
//     their own `Ctrl+X` -- Stage 5's I-04 turned round, as T10 checked `Ctrl+V`.
//   * A-96: `comment.export` exports what the Comments pane LISTS -- filtered, the filter's comments and their threads.
//   * An import through `comment.import`: one command, the refusals in the report by name.
//   * Convention 16: the three commands that ask for a file are disabled where nobody can be asked.
//
// WHAT IT DOES NOT COVER (convention 19): the backend is `tests/ui/FakeBackend.h` -- its sink accepts what it is told,
// its exchange files are XFDF only, and what is drawn and saved is the composition suite's
// (`tests/composition/tst_exchangeproduct`). Offscreen, the clipboard is the process's own, not Windows'.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/doc/XfdfCodec.h"
#include "tests/ui/ShellFixture.h"
#include "ui/panes/CommentsPane.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/StatusBar.h"
#include "ui/shell/ToolFinder.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/write/FindReplaceBar.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMimeData>

#include <set>

using namespace rawpage;
using core::ActionId;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;

namespace {

Annotation held(long long object, AnnotationSubtype subtype, const QString& author, const QString& contents, core::PdfRect rect)
{
    Annotation a;
    a.id = AnnotationId::ofObject(object);
    a.subtype = subtype;
    a.page = 0;
    a.rect = rect;
    a.author = author;
    a.contents = contents;
    a.nm = QStringLiteral("held-%1").arg(object);
    a.color = {1, 0.85, 0.3};
    a.created = core::PdfDate::parse(QStringLiteral("D:20260901090000Z"));
    a.modified = a.created;
    a.flags = core::AnnotationFlag::Print;
    a.hasAppearance = subtype != AnnotationSubtype::Popup;
    return a;
}

// A square by Ana, a note by Ben with its pop-up, and Ana's reply to the note -- all on page 1.
core::AnnotationStore startingStore()
{
    std::vector<Annotation> all;
    all.push_back(held(20, AnnotationSubtype::Square, QStringLiteral("Ana"), QStringLiteral("a square"),
                       core::PdfRect{core::PdfPoint{400, 700}, core::PdfSize{100, 50}}));
    Annotation note = held(21, AnnotationSubtype::Text, QStringLiteral("Ben"), QStringLiteral("a note"),
                           core::PdfRect{core::PdfPoint{100, 600}, core::PdfSize{20, 20}});
    note.icon = QStringLiteral("Comment");
    note.popup = AnnotationId::ofObject(22);
    all.push_back(note);
    Annotation popup = held(22, AnnotationSubtype::Popup, QString(), QString(), core::PdfRect{core::PdfPoint{130, 520}, core::PdfSize{180, 100}});
    popup.nm.clear();
    popup.parent = AnnotationId::ofObject(21);
    popup.open = false;
    all.push_back(popup);
    Annotation reply = held(23, AnnotationSubtype::Text, QStringLiteral("Ana"), QStringLiteral("a reply"),
                            core::PdfRect{core::PdfPoint{100, 600}, core::PdfSize{20, 20}});
    reply.icon = QStringLiteral("Comment");
    reply.inReplyTo = AnnotationId::ofObject(21);
    all.push_back(reply);

    core::AnnotationStore::Reading reading;
    reading.pages.resize(2);
    for (std::size_t i = 0; i < all.size(); ++i) {
        core::AnnotationListingEntry entry;
        entry.fact = core::AnnotationFact{true, all[i].subtype, all[i].rect};
        entry.annotation = i;
        reading.pages[0].push_back(entry);
    }
    reading.annotations = std::move(all);
    return core::AnnotationStore::fromReading(std::move(reading));
}

struct Exchanging {
    uitest::ShellFixture fixture;
    std::unique_ptr<ui::MainWindow> window;

    Exchanging()
    {
        fixture.backend.pagesPerDocument = 2;
        fixture.backend.editable = true;
        fixture.backend.annotations_ = startingStore();
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 900);
        window->show();
        open(QStringLiteral("first.pdf"));
    }

    ~Exchanging()
    {
        ui::MainWindow::setExchangeAnswersForTesting({});
        if (QClipboard* clipboard = QGuiApplication::clipboard()) clipboard->clear();
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    QString path(const QString& name) const { return QDir(fixture.directory.path()).filePath(name); }

    void open(const QString& name)
    {
        const QString target = path(name);
        QFile file(target);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("the fake backend decides, not these bytes");
        file.close();
        window->openFiles({QFileInfo(target).absoluteFilePath()});
        for (QWidget* widget : window->findChildren<QWidget*>())
            if (QLayout* layout = widget->layout()) layout->activate();
        QCoreApplication::processEvents();
        REQUIRE(window->currentView() != nullptr);
        REQUIRE(window->currentView()->width() > 300);   // the geometry first (convention 12)
        REQUIRE(overlay()->isActive());
        overlay()->setClock([] { return std::int64_t{1791764400}; });
        window->raiseToForeground();
        window->currentView()->setFocus();
        QCoreApplication::processEvents();
    }

    ui::DocumentWindow* document() const { return window->currentDocument(); }
    ui::AnnotationOverlay* overlay() const { return document()->annotations(); }
    const core::AnnotationStore& store() const { return document()->journal()->document().annotations(); }
    std::size_t position() const { return document()->journal()->position(); }

    static void key(QWidget* target, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QKeyEvent override(QEvent::ShortcutOverride, key, modifiers);
        override.ignore();
        QCoreApplication::sendEvent(target, &override);
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        press.ignore();
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QCoreApplication::sendEvent(target, &release);
        QCoreApplication::processEvents();
    }

    std::vector<Annotation> created() const
    {
        std::vector<Annotation> out;
        for (const Annotation& a : store().all())
            if (a.id.kind == AnnotationId::Kind::Created) out.push_back(a);
        return out;
    }
};

// What a paste must keep: every modelled field but the identity, the name, the page and the references.
QJsonObject kept(const Annotation& a)
{
    QJsonObject json = a.toJson();
    for (const char* key : {"id", "nm", "page", "inReplyTo", "popup", "parent", "hasAppearance"}) json.remove(QLatin1String(key));
    return json;
}

} // namespace

TEST_CASE("annotations copied are pasted into another tab with new names and every other field as copied",
          "[exchange][A-81][D6-22]")
{
    Exchanging e;
    e.overlay()->select({AnnotationId::ofObject(20), AnnotationId::ofObject(21)});
    QCoreApplication::processEvents();
    const std::size_t before = e.position();
    Exchanging::key(e.window->currentView(), Qt::Key_C, Qt::ControlModifier);
    CHECK(e.position() == before);   // a copy changes nothing
    const QMimeData* held = QGuiApplication::clipboard()->mimeData();
    REQUIRE(held != nullptr);
    REQUIRE(held->hasFormat(QLatin1String(ui::MainWindow::kAnnotationsMimeType)));
    CHECK(held->text() == QStringLiteral("a square\na note"));
    const QByteArray payload = held->data(QLatin1String(ui::MainWindow::kAnnotationsMimeType));

    // INTO ANOTHER TAB: one command, the square, the note and the note's pop-up.
    e.open(QStringLiteral("second.pdf"));
    const std::size_t second = e.position();
    Exchanging::key(e.window->currentView(), Qt::Key_V, Qt::ControlModifier);
    CHECK(e.position() == second + 1);
    const std::vector<Annotation> pasted = e.created();
    REQUIRE(pasted.size() == 3);
    std::set<QString> names;
    const Annotation* note = nullptr;
    const Annotation* popup = nullptr;
    for (const Annotation& a : pasted) {
        names.insert(a.nm);
        CHECK(a.nm.size() == 32);
        CHECK_FALSE(a.nm.startsWith(QLatin1String("held-")));
        const std::optional<std::size_t> original = e.store().findObject(a.subtype == AnnotationSubtype::Square ? 20
                                                                       : a.subtype == AnnotationSubtype::Text ? 21 : 22);
        REQUIRE(original);
        INFO(core::annotationSubtypeName(a.subtype).data());
        CHECK(QJsonDocument(kept(a)).toJson() == QJsonDocument(kept(e.store().at(*original))).toJson());
        if (a.subtype == AnnotationSubtype::Text) note = &a;
        if (a.subtype == AnnotationSubtype::Popup) popup = &a;
    }
    CHECK(names.size() == 3);
    REQUIRE(note != nullptr);
    REQUIRE(popup != nullptr);
    CHECK(note->popup == popup->id);   // the pair still names each other
    CHECK(popup->parent == note->id);
    // The pasted comments are selected, the pop-up with nothing of its own to select.
    CHECK(e.overlay()->selection().size() == 2);

    // AS A SECOND PROCESS WOULD HAVE THEM: the bytes alone, into a third tab.
    e.open(QStringLiteral("third.pdf"));
    const std::size_t third = e.position();
    REQUIRE(e.window->pasteAnnotations(payload));
    CHECK(e.position() == third + 1);
    CHECK(e.created().size() == 3);
    // Bytes that are not RawPage's comments paste nothing, said by key.
    const core::Status refused = e.window->pasteAnnotations(QByteArray("{\"version\":1,\"annotations\":[7]}"));
    REQUIRE_FALSE(refused);
    CHECK(QString(refused.error().messageKey) == QLatin1String("error.clipboard.malformed"));
    CHECK(e.position() == third + 1);
}

TEST_CASE("Ctrl+X cuts the selected annotation as a copy and one delete", "[exchange][A-81][D6-22]")
{
    Exchanging e;
    e.overlay()->select({AnnotationId::ofObject(20)});
    QCoreApplication::processEvents();
    REQUIRE(e.fixture.actions.isEnabled(ActionId::EditCut));
    const std::size_t before = e.position();
    Exchanging::key(e.window->currentView(), Qt::Key_X, Qt::ControlModifier);
    CHECK(e.position() == before + 1);
    const std::optional<core::CatalogText> step = e.document()->journal()->undoName();
    REQUIRE(step);
    CHECK(QString(step->key) == QLatin1String("command.annot.delete"));
    CHECK_FALSE(e.store().findObject(20).has_value());
    REQUIRE(QGuiApplication::clipboard()->mimeData()->hasFormat(QLatin1String(ui::MainWindow::kAnnotationsMimeType)));
    CHECK(QGuiApplication::clipboard()->mimeData()->text() == QStringLiteral("a square"));
    // With nothing selected, there is nothing to cut: the command is disabled, not a no-op.
    e.overlay()->clearSelection();
    QCoreApplication::processEvents();
    CHECK_FALSE(e.fixture.actions.isEnabled(ActionId::EditCut));
}

TEST_CASE("the find bar the properties bar and the tool finder keep their own Ctrl+X", "[exchange][A-81][D6-22]")
{
    Exchanging e;
    std::vector<QLineEdit*> fields;
    ui::FindReplaceBar* find = e.document()->findBar();
    REQUIRE(find != nullptr);
    find->open();
    fields.push_back(find->field());
    if (!e.document()->propertiesBarShown()) e.document()->togglePropertiesBar();
    e.overlay()->select({AnnotationId::ofObject(21)});   // a note, whose words the bar's field edits
    QCoreApplication::processEvents();
    fields.push_back(e.document()->propertiesBar()->textField());
    fields.push_back(e.window->toolFinder()->searchBox());
    const std::size_t before = e.position();
    for (QLineEdit* field : fields) {
        INFO(field->objectName().toStdString());
        REQUIRE(field->isEnabled());
        field->setFocus(Qt::OtherFocusReason);
        field->setText(QStringLiteral("cut these words"));
        field->selectAll();
        QCoreApplication::processEvents();
        Exchanging::key(field, Qt::Key_X, Qt::ControlModifier);
        CHECK(field->text().isEmpty());
        CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("cut these words"));
        CHECK(e.position() == before);   // the selected note was NOT cut
        CHECK(e.store().findObject(21).has_value());
    }
}

TEST_CASE("comment.export exports what the Comments pane lists and its threads", "[exchange][A-96][D6-20]")
{
    Exchanging e;
    const QString out = e.path(QStringLiteral("filtered.xfdf"));
    ui::MainWindow::setExchangeAnswersForTesting({{}, [out](const QString&) { return std::optional<QString>(out); }, {}});
    REQUIRE(e.fixture.actions.dispatch(ActionId::PaneComments, core::KeyContext::Global));
    ui::CommentsPane* pane = e.window->commentsPane();
    REQUIRE(pane != nullptr);
    pane->setAuthorFilter({QStringLiteral("Ben")});
    QCoreApplication::processEvents();
    REQUIRE(e.fixture.actions.isEnabled(ActionId::CommentExport));
    REQUIRE(e.fixture.actions.dispatch(ActionId::CommentExport, core::KeyContext::Global));
    REQUIRE(e.fixture.backend.exchangesWritten.size() == 1);
    const auto& written = e.fixture.backend.exchangesWritten.front();
    CHECK(written.path == out);
    CHECK(written.format == core::ExchangeFormat::Xfdf);
    std::set<QString> names;
    for (const Annotation& a : written.exchange.annotations) names.insert(a.id.nm);
    // Ben's note, its pop-up and Ana's reply to it -- and not Ana's square.
    CHECK(names == std::set<QString>{QStringLiteral("held-21"), core::popupNameFor(QStringLiteral("held-21")), QStringLiteral("held-23")});

    // THE CONTROL: unfiltered, everything.
    pane->clearFilter();
    QCoreApplication::processEvents();
    REQUIRE(e.fixture.actions.dispatch(ActionId::CommentExport, core::KeyContext::Global));
    REQUIRE(e.fixture.backend.exchangesWritten.size() == 2);
    CHECK(e.fixture.backend.exchangesWritten.back().exchange.annotations.size() == 4);
}

TEST_CASE("comment.import merges a file as one command and reports what it did not import by name", "[exchange][A-94][D6-20]")
{
    Exchanging e;
    core::Fdf exchange;
    Annotation fresh = core::newAnnotation(AnnotationSubtype::Square, 1, QStringLiteral("Zoë"), 1791764400);
    fresh.nm = QStringLiteral("imported-square");
    fresh.id = AnnotationId::ofCreated(fresh.nm);
    fresh.rect = core::PdfRect{core::PdfPoint{50, 50}, core::PdfSize{80, 40}};
    Annotation far = fresh;
    far.nm = QStringLiteral("on-page-six");
    far.id = AnnotationId::ofCreated(far.nm);
    far.page = 5;
    Annotation known = e.store().at(*e.store().findObject(20));
    known.id = AnnotationId::ofCreated(known.nm);   // the document's square, as an exchange names it, not newer
    exchange.annotations = {fresh, far, known};
    const QString file = e.path(QStringLiteral("incoming.xfdf"));
    {
        QFile out(file);
        REQUIRE(out.open(QIODevice::WriteOnly));
        out.write(core::XfdfCodec::write(exchange));
    }
    ui::MainWindow::setExchangeAnswersForTesting({[file] { return std::optional<QString>(file); }, {}, {}});
    REQUIRE(e.fixture.actions.isEnabled(ActionId::CommentImport));
    const std::size_t before = e.position();
    REQUIRE(e.fixture.actions.dispatch(ActionId::CommentImport, core::KeyContext::Global));
    CHECK(e.position() == before + 1);
    const std::optional<core::CatalogText> step = e.document()->journal()->undoName();
    REQUIRE(step);
    CHECK(QString(step->key) == QLatin1String("command.annot.import"));
    CHECK(e.store().find(AnnotationId::ofCreated(QStringLiteral("imported-square"))).has_value());

    REQUIRE(e.window->lastExchangeReport().has_value());
    const ui::ExchangeReport& report = *e.window->lastExchangeReport();
    REQUIRE(report.rows.size() == 3);
    CHECK(report.rows[1].name == QStringLiteral("on-page-six"));
    CHECK(report.rows[1].reason.contains(QLatin1String("6")));
    CHECK_FALSE(report.rows[1].reason.isEmpty());
    CHECK(report.rows[2].reason.isEmpty());   // kept: the document's is as new
    CHECK(e.window->statusBar()->message()->text() == report.summary);
}

TEST_CASE("the three commands that ask for a file are disabled where nobody can be asked", "[exchange][D6-20][convention-16]")
{
    Exchanging e;
    ui::MainWindow::setExchangeAnswersForTesting({});
    CHECK_FALSE(e.fixture.actions.isEnabled(ActionId::CommentImport));
    CHECK_FALSE(e.fixture.actions.isEnabled(ActionId::CommentExport));
    CHECK_FALSE(e.fixture.actions.isEnabled(ActionId::CommentMigrate));
    ui::MainWindow::setExchangeAnswersForTesting({[] { return std::optional<QString>(); },
                                                  [](const QString&) { return std::optional<QString>(); },
                                                  [] { return std::optional<QString>(); }});
    CHECK(e.fixture.actions.isEnabled(ActionId::CommentImport));
    CHECK(e.fixture.actions.isEnabled(ActionId::CommentExport));
    CHECK(e.fixture.actions.isEnabled(ActionId::CommentMigrate));
    // A file nobody chose imports nothing and changes nothing.
    const std::size_t before = e.position();
    REQUIRE(e.fixture.actions.dispatch(ActionId::CommentImport, core::KeyContext::Global));
    CHECK(e.position() == before);
}
