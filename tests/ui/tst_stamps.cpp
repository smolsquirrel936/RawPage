// tst_stamps -- Stage 6 T10 through the widgets: the palette, the custom-stamp dialog, the turn on the properties bar
// and an image pasted (`Stage6-Annotations-Testing-Guide.md` §5.13; D6-16, D6-22).
//
//   * `Ctrl+Alt+S` from the page, as a KEY EVENT through the application: the stamp tool armed and the palette shown
//     (A-40's stamp clause).
//   * The palette lists Table 181's fourteen and the five dynamic stamps; choosing one makes it the tool's and arms
//     the tool; a window composed with no library offers no custom stamp.
//   * A custom stamp added through the window, over a SCRATCH library: listed under its category and Favourites,
//     chosen with its pixels decoded once, refiled, unfavoured and deleted -- each written at once (A-79's palette
//     half).
//   * What M-07's walk asked for (S6 guide I-39 … I-41): the highlight goes when the stamp tool is put away, so one
//     click on the same stamp arms it again; Favourites first even when empty, a right click adding a stamp to it or
//     taking it out, and a right press arming nothing; and Move to… offering the categories in use and a new one.
//   * A-78's dialog half: the dialog's browser lists image types only; a PDF is refused BY NAME and nothing can be
//     made; a PNG is kept as its bytes, any other image as a PNG of itself; drawn and typed initials are PNGs filed
//     under Signatures.
//   * A-80's bar half: the properties bar's turn is one command for a stamp, disabled for anything else.
//   * A-81's image clause: an image on the clipboard and `Ctrl+V` on the page is ONE image stamp in the middle of
//     what is shown -- and the find bar, the properties bar's text field and the tool finder each keep their own
//     `Ctrl+V` (Stage 5's I-04 turned round).
//
// WHAT IT DOES NOT COVER (convention 19): pixels (`tests/composition/tst_stampink`), the file
// (`tests/unit/tst_stamps`), the system clipboard of Windows -- offscreen, the clipboard is the process's own -- and
// annotations copied and pasted, which are T13's (A-81's other clauses).

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/doc/Stamps.h"
#include "core/settings/ProfileStore.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/CustomStampDialog.h"
#include "ui/dialogs/StampCategoryDialog.h"
#include "ui/panes/StampPalettePane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ToolFinder.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/tools/StampTool.h"
#include "ui/write/FindReplaceBar.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

using namespace rawpage;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;

namespace {

constexpr Qt::KeyboardModifiers kCtrlAlt = Qt::ControlModifier | Qt::AltModifier;

QByteArray corpusBytes(const char* relative)
{
    QFile file(QStringLiteral(RAWPAGE_CORPUS_DIR "/") + QLatin1String(relative));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

core::AnnotationStore startingStore()
{
    core::AnnotationStore::Reading reading;
    reading.pages.resize(2);
    Annotation square;
    square.id = AnnotationId::ofObject(20);
    square.subtype = AnnotationSubtype::Square;
    square.page = 0;
    square.color = {1, 0, 0};
    square.rect = core::PdfRect{core::PdfPoint{400, 700}, core::PdfSize{100, 50}};
    square.hasAppearance = true;
    core::AnnotationListingEntry entry;
    entry.fact = core::AnnotationFact{true, square.subtype, square.rect};
    entry.annotation = 0;
    reading.pages[0].push_back(entry);
    reading.annotations.push_back(square);
    return core::AnnotationStore::fromReading(std::move(reading));
}

struct Fixture : uitest::ShellFixture {
    QString writeFile(const QString& name)
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("the fake backend decides, not these bytes");
        file.close();
        return QFileInfo(path).absoluteFilePath();
    }
};

struct Stamping {
    Fixture fixture;
    std::unique_ptr<core::ProfileStore> library;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;
    ui::PageView* view = nullptr;
    ui::AnnotationOverlay* overlay = nullptr;

    // `withLibrary`: a SCRATCH library under the fixture's directory -- never the stamps of whoever runs the suite.
    explicit Stamping(bool withLibrary = false)
    {
        ui::setCurrentStamp(ui::StampChoice{});   // the choice is the process's, and the next case shares it
        fixture.backend.pagesPerDocument = 2;
        fixture.backend.editable = true;
        fixture.backend.annotations_ = startingStore();
        app::Services services = fixture.services();
        if (withLibrary) {
            library = std::make_unique<core::ProfileStore>(QDir(fixture.directory.path()).filePath(QStringLiteral("stamps")));
            library->load();
            services.stamps = library.get();
        }
        window = std::make_unique<ui::MainWindow>(services);
        window->resize(1100, 900);
        window->openFiles({fixture.writeFile(QStringLiteral("stamping.pdf"))});
        window->show();
        for (QWidget* widget : window->findChildren<QWidget*>())
            if (QLayout* layout = widget->layout()) layout->activate();
        QCoreApplication::processEvents();
        document = window->currentDocument();
        REQUIRE(document != nullptr);
        view = window->currentView();
        REQUIRE(view != nullptr);
        REQUIRE(view->width() > 300);   // the geometry first (convention 12)
        overlay = document->annotations();
        REQUIRE(overlay != nullptr);
        REQUIRE(overlay->isActive());
        overlay->setClock([] { return std::int64_t{1791106200}; });
        window->raiseToForeground();
        view->setFocus();
        QCoreApplication::processEvents();
    }

    ~Stamping()
    {
        ui::setCurrentStamp(ui::StampChoice{});
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    // A key to `target`, through the application, as a person presses it.
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

    // A left click `offset` from the middle of the page view, as a person places a stamp.
    void clickPage(QPoint offset = QPoint()) const
    {
        const QPoint pos = view->viewport()->rect().center() + offset;
        for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            QMouseEvent event(type, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)), Qt::LeftButton,
                              type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view->viewport(), &event);
            QCoreApplication::processEvents();
        }
    }

    std::size_t journalPosition() const { return document->journal()->position(); }
    std::vector<Annotation> created() const
    {
        std::vector<Annotation> out;
        for (const Annotation& a : document->journal()->document().annotations().all())
            if (a.id.kind == AnnotationId::Kind::Created) out.push_back(a);
        return out;
    }
};

// The children of the palette's heading titled `title`.
std::vector<QString> keysUnder(const ui::StampPalettePane& palette, const QString& title)
{
    std::vector<QString> keys;
    QTreeWidget* tree = palette.tree();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* heading = tree->topLevelItem(i);
        if (heading->text(0) != title) continue;
        for (int k = 0; k < heading->childCount(); ++k) keys.push_back(heading->child(k)->data(0, Qt::UserRole + 1).toString());
    }
    return keys;
}

// The palette's item for `key` under its own category -- never the copy under Favourites.
QTreeWidgetItem* itemWithKey(const ui::StampPalettePane& palette, const QString& key)
{
    const QString favourites = ui::catalog::translate(QStringLiteral("stamps.category.favourites"));
    QTreeWidget* tree = palette.tree();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* heading = tree->topLevelItem(i);
        if (heading->text(0) == favourites) continue;
        for (int k = 0; k < heading->childCount(); ++k)
            if (heading->child(k)->data(0, Qt::UserRole + 1).toString() == key) return heading->child(k);
    }
    return nullptr;
}

QString keyOfItem(const QTreeWidgetItem* item)
{
    return item != nullptr ? item->data(0, Qt::UserRole + 1).toString() : QString();
}

} // namespace

TEST_CASE("Ctrl+Alt+S from the page arms the stamp tool and shows the palette", "[stamps][A-40]")
{
    Stamping s;
    ui::StampPalettePane* palette = s.window->stampPalette();
    REQUIRE(palette != nullptr);
    CHECK_FALSE(palette->isVisible());
    Stamping::key(s.view, Qt::Key_S, kCtrlAlt);
    CHECK(s.view->tools().armedAnnotation() == ui::AnnotationToolKind::Stamp);
    CHECK(palette->isVisible());
}

TEST_CASE("the palette lists the fourteen and the five and a choice is the tool's", "[stamps][D6-16]")
{
    Stamping s;   // no library: built-in stamps only
    ui::StampPalettePane* palette = s.window->stampPalette();
    REQUIRE(palette != nullptr);
    QTreeWidget* tree = palette->tree();
    std::size_t standard = 0, dynamic = 0, other = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        for (int k = 0; k < tree->topLevelItem(i)->childCount(); ++k) {
            const QString key = tree->topLevelItem(i)->child(k)->data(0, Qt::UserRole + 1).toString();
            if (key.startsWith(QLatin1String("standard:"))) ++standard;
            else if (key.startsWith(QLatin1String("dynamic:"))) ++dynamic;
            else ++other;
        }
    }
    CHECK(standard == 14);
    CHECK(dynamic == 5);
    CHECK(other == 0);
    // No library, no custom stamp: the button is there and does nothing.
    CHECK_FALSE(palette->findChild<QPushButton*>(QStringLiteral("stampCustom"))->isEnabled());
    CHECK_FALSE(s.fixture.actions.isEnabled(core::ActionId::AnnotCustomStamp));

    // A CLICK on an item: the stamp the tool places, and the tool armed.
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        for (int k = 0; k < tree->topLevelItem(i)->childCount(); ++k) {
            QTreeWidgetItem* item = tree->topLevelItem(i)->child(k);
            if (item->data(0, Qt::UserRole + 1).toString() == QLatin1String("dynamic:DynamicRevised")) tree->setCurrentItem(item);
        }
    }
    QCoreApplication::processEvents();
    CHECK(ui::currentStamp().kind == ui::StampChoice::Kind::Dynamic);
    CHECK(ui::currentStamp().name == QStringLiteral("DynamicRevised"));
    CHECK(palette->chosenKey() == QStringLiteral("dynamic:DynamicRevised"));
    CHECK(s.view->tools().armedAnnotation() == ui::AnnotationToolKind::Stamp);
    CHECK_FALSE(palette->choose(QStringLiteral("standard:SBApproved")));   // a name of neither list
}

TEST_CASE("a custom stamp is listed, chosen, refiled, unfavoured and deleted through the window", "[stamps][A-79][D6-16]")
{
    Stamping s(true);
    ui::StampPalettePane* palette = s.window->stampPalette();
    REQUIRE(palette->library() == s.library.get());
    CHECK(s.fixture.actions.isEnabled(core::ActionId::AnnotCustomStamp));

    ui::CustomStampDialog::Made made;
    made.name = QStringLiteral("Office seal");
    made.category = QStringLiteral("Office");
    made.favourite = true;
    made.image = corpusBytes("stamps/seal-rgba.png");
    REQUIRE(s.window->addCustomStamp(made));
    REQUIRE(s.library->stamps().size() == 1);
    const QString key = QStringLiteral("custom:") + s.library->stamps()[0].id;
    CHECK(keysUnder(*palette, QStringLiteral("Office")) == std::vector<QString>{key});
    CHECK(keysUnder(*palette, ui::catalog::translate(QStringLiteral("stamps.category.favourites"))) == std::vector<QString>{key});
    // Chosen, its pixels decoded ONCE: the PNG's 96 x 96 with its alpha, and the tool armed.
    CHECK(palette->chosenKey() == key);
    CHECK(ui::currentStamp().kind == ui::StampChoice::Kind::Custom);
    CHECK(ui::currentStamp().image.encoding == core::StampImage::Encoding::Flate);
    CHECK(ui::currentStamp().image.width == 96);
    CHECK_FALSE(ui::currentStamp().image.alpha.isEmpty());
    CHECK(s.view->tools().armedAnnotation() == ui::AnnotationToolKind::Stamp);

    // Refiled and unfavoured -- each WRITTEN at once, as a second store over the same root reads.
    REQUIRE(palette->fileUnder(QStringLiteral("Seals")));
    REQUIRE(palette->setFavourite(key, false));
    core::ProfileStore again(s.library->root());
    again.load();
    REQUIRE(again.stamps().size() == 1);
    CHECK(again.stamps()[0].category == QStringLiteral("Seals"));
    CHECK_FALSE(again.stamps()[0].favourite);
    CHECK(keysUnder(*palette, QStringLiteral("Seals")) == std::vector<QString>{key});
    // A built-in stamp may be a favourite, and cannot be refiled or deleted.
    REQUIRE(palette->choose(QStringLiteral("standard:Draft")));
    REQUIRE(palette->setFavourite(QStringLiteral("standard:Draft"), true));
    CHECK(s.library->isFavourite(QStringLiteral("Draft")));
    CHECK_FALSE(palette->fileUnder(QStringLiteral("Mine")));
    CHECK_FALSE(palette->deleteChosen());

    REQUIRE(palette->choose(key));
    REQUIRE(palette->deleteChosen());
    CHECK(s.library->stamps().empty());
    CHECK(keysUnder(*palette, QStringLiteral("Seals")).empty());
    CHECK(ui::currentStamp().kind == ui::StampChoice::Kind::Standard);   // the tool falls back to a built-in
}

// S6 guide I-39: a stamp placed left its palette item current, so a click on it changed nothing and armed nothing
// -- placing the same stamp again took a click on another first.
TEST_CASE("the palette's highlight goes when the stamp tool is put away and one click on the same stamp arms it again",
          "[stamps][I-39]")
{
    Stamping s;
    ui::StampPalettePane* palette = s.window->stampPalette();
    QTreeWidget* tree = palette->tree();
    // Armed by its chord, nothing clicked in the palette: the stamp it places is the one highlighted.
    Stamping::key(s.view, Qt::Key_S, kCtrlAlt);
    REQUIRE(s.view->tools().armedAnnotation() == ui::AnnotationToolKind::Stamp);
    CHECK(keyOfItem(tree->currentItem()) == QStringLiteral("standard:Approved"));

    QTreeWidgetItem* draft = itemWithKey(*palette, QStringLiteral("standard:Draft"));
    REQUIRE(draft != nullptr);
    tree->setCurrentItem(draft);   // a click
    QCoreApplication::processEvents();
    REQUIRE(ui::currentStamp().name == QStringLiteral("Draft"));

    // Placed: the tool put away, and the highlight with it.
    s.clickPage();
    REQUIRE(s.created().size() == 1u);
    CHECK_FALSE(s.view->tools().armedAnnotation() == ui::AnnotationToolKind::Stamp);
    CHECK(tree->currentItem() == nullptr);
    CHECK(tree->selectedItems().isEmpty());
    CHECK(palette->chosenKey().isEmpty());

    // The SAME stamp, one click: armed again, and placed again.
    tree->setCurrentItem(draft);
    QCoreApplication::processEvents();
    CHECK(s.view->tools().armedAnnotation() == ui::AnnotationToolKind::Stamp);
    CHECK(palette->chosenKey() == QStringLiteral("standard:Draft"));
    s.clickPage(QPoint(0, 150));   // clear of the first
    CHECK(s.created().size() == 2u);
    CHECK(tree->currentItem() == nullptr);

    // `Esc` puts it away as a placement does; the chord brings back the highlight on what it places, Draft.
    tree->setCurrentItem(draft);
    QCoreApplication::processEvents();
    REQUIRE(s.view->tools().armedAnnotation() == ui::AnnotationToolKind::Stamp);
    Stamping::key(s.view, Qt::Key_Escape);
    CHECK(tree->currentItem() == nullptr);
    Stamping::key(s.view, Qt::Key_S, kCtrlAlt);
    CHECK(keyOfItem(tree->currentItem()) == QStringLiteral("standard:Draft"));
}

// S6 guide I-41: a Favourite button the user found no use for, and a Favourites heading shown only once it held a
// stamp. NOT covered (convention 19): the menu under a real pointer -- Windows opens it on the right RELEASE, which
// reaches the view; here the press is sent and the menu asked for by `showItemMenu`, as the view's signal would.
TEST_CASE("Favourites heads the palette even empty and a right click adds a stamp to it or takes it out",
          "[stamps][I-41]")
{
    Stamping s(true);
    s.window->showStampPalette();
    for (QWidget* widget : s.window->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    QCoreApplication::processEvents();
    ui::StampPalettePane* palette = s.window->stampPalette();
    QTreeWidget* tree = palette->tree();
    REQUIRE(tree->viewport()->height() > 100);   // the geometry first (convention 12)
    const QString favourites = ui::catalog::translate(QStringLiteral("stamps.category.favourites"));
    REQUIRE(tree->topLevelItemCount() > 1);
    CHECK(tree->topLevelItem(0)->text(0) == favourites);
    CHECK(tree->topLevelItem(0)->childCount() == 0);
    CHECK_FALSE(tree->topLevelItem(0)->toolTip(0).isEmpty());
    CHECK(palette->findChild<QToolButton*>(QStringLiteral("stampFavourite")) == nullptr);   // the button is gone

    const auto rightPress = [tree](QPoint at) {
        for (const QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            QMouseEvent event(type, QPointF(at), QPointF(tree->viewport()->mapToGlobal(at)), Qt::RightButton,
                              type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::RightButton, Qt::NoModifier);
            QCoreApplication::sendEvent(tree->viewport(), &event);
        }
        QCoreApplication::processEvents();
    };
    const auto pointOf = [tree, palette](const QString& key) {
        QTreeWidgetItem* item = itemWithKey(*palette, key);
        REQUIRE(item != nullptr);
        tree->scrollToItem(item);
        QCoreApplication::processEvents();
        return tree->visualItemRect(item).center();
    };
    const auto menuRow = [palette]() {
        const QList<QAction*> rows = palette->itemMenu()->actions();
        REQUIRE(rows.size() == 1);
        return rows.front();
    };

    // A RIGHT PRESS on a stamp chooses nothing and arms nothing.
    QPoint at = pointOf(QStringLiteral("standard:Draft"));
    rightPress(at);
    CHECK(keyOfItem(tree->currentItem()) != QStringLiteral("standard:Draft"));
    CHECK_FALSE(s.view->tools().armedAnnotation() == ui::AnnotationToolKind::Stamp);

    // Its menu's one row adds it: written at once, and listed first under Favourites.
    REQUIRE(palette->showItemMenu(at));
    CHECK(menuRow()->text() == ui::catalog::translate(QStringLiteral("stamps.favourite.add")));
    menuRow()->trigger();
    palette->itemMenu()->close();
    CHECK(s.library->isFavourite(QStringLiteral("Draft")));
    CHECK(tree->topLevelItem(0)->text(0) == favourites);
    CHECK(keysUnder(*palette, favourites) == std::vector<QString>{QStringLiteral("standard:Draft")});

    // The same stamp's menu now takes it out; the heading stays, empty.
    at = pointOf(QStringLiteral("standard:Draft"));
    REQUIRE(palette->showItemMenu(at));
    CHECK(menuRow()->text() == ui::catalog::translate(QStringLiteral("stamps.favourite.remove")));
    menuRow()->trigger();
    palette->itemMenu()->close();
    CHECK_FALSE(s.library->isFavourite(QStringLiteral("Draft")));
    CHECK(tree->topLevelItem(0)->text(0) == favourites);
    CHECK(tree->topLevelItem(0)->childCount() == 0);

    // A heading has no menu.
    tree->scrollToItem(tree->topLevelItem(1));
    QCoreApplication::processEvents();
    CHECK_FALSE(palette->showItemMenu(tree->visualItemRect(tree->topLevelItem(1)).center()));
}

// S6 guide I-40: Move to… asked for a category as free text, so a person had to remember and retype one.
// NOT covered (convention 19): *New…*'s own prompt, a `QInputDialog` offscreen cannot answer -- `addCategory` is
// what it does with the name typed; and the dialog's `exec` from the palette's button.
TEST_CASE("Move to offers the categories in use and New adds one", "[stamps][I-40]")
{
    ui::StampCategoryDialog dialog(QStringList{QLatin1String(core::ProfileStore::kSignaturesCategory),
                                               QStringLiteral("Office"), QStringLiteral("Seals")},
                                   QStringLiteral("Office"));
    QComboBox* list = dialog.list();
    CHECK_FALSE(list->isEditable());   // chosen from the list, never retyped
    REQUIRE(list->count() == 3);
    CHECK(list->itemText(0) == ui::catalog::translate(QStringLiteral("stamps.category.signatures")));
    CHECK(list->itemData(0).toString() == QLatin1String(core::ProfileStore::kSignaturesCategory));
    CHECK(dialog.chosenCategory() == QStringLiteral("Office"));   // the stamp's own, first
    CHECK(dialog.newButton()->text() == ui::catalog::translate(QStringLiteral("stamps.refile.new")));

    CHECK(dialog.addCategory(QStringLiteral("  Approvals ")));
    CHECK(dialog.chosenCategory() == QStringLiteral("Approvals"));
    CHECK(list->count() == 4);
    CHECK(dialog.addCategory(QStringLiteral("Seals")));   // one in the list is chosen, not added twice
    CHECK(dialog.chosenCategory() == QStringLiteral("Seals"));
    CHECK(dialog.addCategory(ui::catalog::translate(QStringLiteral("stamps.category.signatures"))));
    CHECK(dialog.chosenCategory() == QLatin1String(core::ProfileStore::kSignaturesCategory));
    CHECK(list->count() == 4);
    CHECK_FALSE(dialog.addCategory(QStringLiteral("   ")));
    CHECK(dialog.chosenCategory() == QLatin1String(core::ProfileStore::kSignaturesCategory));

    // Signatures is offered with a library that has none filed there.
    ui::StampCategoryDialog empty(QStringList{}, QString());
    REQUIRE(empty.list()->count() == 1);
    CHECK(empty.chosenCategory() == QLatin1String(core::ProfileStore::kSignaturesCategory));
}

TEST_CASE("the custom stamp dialog takes images only and refuses a PDF by name", "[stamps][A-78]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    ui::CustomStampDialog dialog(QStringList{QStringLiteral("Office")});
    // The browser's filter names image types and no other.
    const QString filter = ui::CustomStampDialog::imageFilter();
    CHECK(filter.contains(QStringLiteral("*.png")));
    CHECK(filter.contains(QStringLiteral("*.jpg")));
    CHECK_FALSE(filter.contains(QStringLiteral("pdf"), Qt::CaseInsensitive));

    // A PDF, offered as a person typing its path would: refused BY NAME, OK disabled, nothing to make.
    const QString pdf = QStringLiteral(RAWPAGE_CORPUS_DIR "/ordinary/text-a4.pdf");
    CHECK_FALSE(dialog.setImagePath(pdf));
    CHECK(dialog.refusal().contains(QStringLiteral("text-a4.pdf")));
    CHECK_FALSE(dialog.made());
    CHECK_FALSE(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());

    // A PNG: kept as its own bytes, named for its file, filed under the first category offered.
    const QString png = QStringLiteral(RAWPAGE_CORPUS_DIR "/stamps/seal-rgba.png");
    REQUIRE(dialog.setImagePath(png));
    auto fromPng = dialog.made();
    REQUIRE(fromPng);
    CHECK(fromPng->image == corpusBytes("stamps/seal-rgba.png"));
    CHECK(fromPng->name == QStringLiteral("seal-rgba"));
    CHECK(fromPng->category == QStringLiteral("Office"));
    CHECK(fromPng->sourceName == QStringLiteral("seal-rgba.png"));
    // A BMP: kept as a PNG of itself.
    const QString bmp = QDir(scratch.path()).filePath(QStringLiteral("dot.bmp"));
    QImage dot(8, 8, QImage::Format_RGB32);
    dot.fill(Qt::red);
    REQUIRE(dot.save(bmp, "BMP"));
    REQUIRE(dialog.setImagePath(bmp));
    CHECK(core::ProfileStore::imageFormatOf(dialog.made()->image) == QStringLiteral("png"));

    // DRAWN initials: a PNG with a transparent ground, under Signatures.
    dialog.setSource(ui::CustomStampDialog::Source::Drawn);
    CHECK_FALSE(dialog.made());   // nothing drawn yet
    dialog.pad()->addStroke({QPointF(20, 60), QPointF(60, 20), QPointF(100, 60)});
    dialog.setName(QStringLiteral("Initials"));
    auto drawn = dialog.made();
    REQUIRE(drawn);
    CHECK(drawn->category == QLatin1String(core::ProfileStore::kSignaturesCategory));
    const QImage drawnImage = QImage::fromData(drawn->image);
    REQUIRE_FALSE(drawnImage.isNull());
    CHECK(drawnImage.hasAlphaChannel());
    CHECK(qAlpha(drawnImage.pixel(0, 0)) == 0);   // the ground is transparent

    // TYPED initials, likewise.
    dialog.setSource(ui::CustomStampDialog::Source::Typed);
    dialog.setTypedInitials(QStringLiteral("AB"));
    auto typed = dialog.made();
    REQUIRE(typed);
    CHECK_FALSE(QImage::fromData(typed->image).isNull());
    CHECK(typed->category == QLatin1String(core::ProfileStore::kSignaturesCategory));
}

TEST_CASE("the properties bar's turn is one command for a stamp and disabled for anything else", "[stamps][A-80]")
{
    Stamping s;
    auto made = core::newStandardStamp(QStringLiteral("Approved"), 0, core::PdfPoint{200, 400}, QStringLiteral("T"), 1791106200);
    REQUIRE(made);
    REQUIRE(s.overlay->addMade({made.value()}));
    const std::size_t placed = s.journalPosition();
    if (!s.document->propertiesBarShown()) s.document->togglePropertiesBar();
    ui::PropertiesBar* bar = s.document->propertiesBar();
    REQUIRE(bar != nullptr);

    s.overlay->select({made.value().id});
    QCoreApplication::processEvents();
    REQUIRE(bar->rotationBox()->isEnabled());
    bar->rotationBox()->setValue(90);
    QCoreApplication::processEvents();
    CHECK(s.journalPosition() == placed + 1);   // one command
    const std::vector<Annotation> after = s.created();
    REQUIRE(after.size() == 1);
    CHECK(after[0].rotate == 90);
    CHECK(std::abs(after[0].rect.size.width - made.value().rect.size.height) < 1e-6);   // turned: its sides swapped

    s.overlay->select({AnnotationId::ofObject(20)});   // the square
    QCoreApplication::processEvents();
    CHECK_FALSE(bar->rotationBox()->isEnabled());
}

TEST_CASE("an image pasted on the page is one image stamp and text fields keep their own Ctrl+V", "[stamps][A-81][D6-22]")
{
    Stamping s;
    QClipboard* clipboard = QGuiApplication::clipboard();
    REQUIRE(clipboard != nullptr);
    QImage picture(40, 20, QImage::Format_ARGB32);
    picture.fill(QColor(0, 128, 255, 200));
    clipboard->setImage(picture);
    QCoreApplication::processEvents();
    REQUIRE(clipboard->mimeData()->hasImage());

    const std::size_t before = s.journalPosition();
    Stamping::key(s.view, Qt::Key_V, Qt::ControlModifier);
    CHECK(s.journalPosition() == before + 1);   // one command
    const std::vector<Annotation> pasted = s.created();
    REQUIRE(pasted.size() == 1);
    CHECK(pasted[0].subtype == AnnotationSubtype::Stamp);
    CHECK(pasted[0].intent == QStringLiteral("StampImage"));
    CHECK(pasted[0].image.width == 40);
    CHECK_FALSE(pasted[0].image.alpha.isEmpty());   // its translucency kept as an /SMask's plane

    // THE TEXT WIDGETS KEEP THEIRS: with text on the clipboard, `Ctrl+V` in each pastes the text and makes nothing.
    clipboard->setText(QStringLiteral("pasted words"));
    QCoreApplication::processEvents();
    const std::size_t afterImage = s.journalPosition();
    std::vector<QLineEdit*> fields;
    ui::FindReplaceBar* find = s.document->findBar();
    REQUIRE(find != nullptr);
    find->open();
    fields.push_back(find->field());
    if (!s.document->propertiesBarShown()) s.document->togglePropertiesBar();
    s.overlay->select({pasted[0].id});
    QCoreApplication::processEvents();
    fields.push_back(s.document->propertiesBar()->textField());
    fields.push_back(s.window->toolFinder()->searchBox());
    for (QLineEdit* field : fields) {
        INFO(field->objectName().toStdString());
        REQUIRE(field->isEnabled());
        field->setFocus(Qt::OtherFocusReason);
        field->clear();
        QCoreApplication::processEvents();
        Stamping::key(field, Qt::Key_V, Qt::ControlModifier);
        CHECK(field->text() == QStringLiteral("pasted words"));
        CHECK(s.journalPosition() == afterImage);
    }
    clipboard->clear();
}
