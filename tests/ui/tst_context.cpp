// Stage 1 T13 -- ui/shell/ShortcutBinder. Guide case A-53, and the T13 exit criterion
// "every §8 Global default resolves correctly".
//
// THREE SUBJECTS, AND THEY FAIL IN DIFFERENT WAYS, so they are asserted separately:
//
//   1. `activeContext()` over synthetic focus chains -- decision D1-5. Parented, never-shown
//      widget trees, exactly as the plan specifies: a focus chain is made of QWidgets, so this
//      cannot live in tests/unit, and it needs no window because a chain is a parent chain.
//
//   2. The QtGui boundary. A key press becomes a `core::Chord`, or it becomes nothing -- and the
//      sweep over §8 is the one that earns its keep: every row in `KeyBindingDefaults.def` is
//      turned back into a key event and read again, so a key this class cannot name is a FAILURE
//      here rather than a binding that silently never fires.
//
//   3. Resolution, dispatch and consumption. The five outcomes exist because collapsing any two of
//      them loses something -- what a batch report needs to say, and whether Qt still gets the key.
//
// Offscreen, so nothing here judges appearance. What it cannot see is a real keyboard: M-12's
// keyboard walk is the manual half, and it is where a chord the WINDOW MANAGER eats first would
// show up, which no synthetic event can reproduce.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "tests/ui/ShellFixture.h"
#include "core/cmd/ActionRegistry.h"
#include "core/LogBuffer.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Ribbon.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/ToolFinder.h"
#include "ui/viewer/PageView.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QShortcut>
#include <QTemporaryDir>
#include <QWidget>

#include <set>

using namespace rawpage;
using namespace rawpage::ui;

namespace {

struct Fixture : uitest::ShellFixture {
    // Every member and `services()` come from `tests/ui/ShellFixture.h` (S2 T13). They were seven
    // copies of the same eight fields until `app::Services` grew a render pipeline and the copies
    // became a seven-way edit; the header states what a fixture deliberately does NOT provide.
};

QString chordText(const core::Chord& chord)
{
    return QString::fromStdString(chord.toString());
}

// Every context a widget may declare -- which is every KeyContext except Common, and Common's
// absence is asserted on its own below rather than quietly skipped here.
std::vector<core::KeyContext> declarableContexts()
{
    std::vector<core::KeyContext> contexts;
    for (std::size_t i = 0; i < core::kKeyContextCount; ++i) {
        const auto context = static_cast<core::KeyContext>(i);
        if (context != core::KeyContext::Common) contexts.push_back(context);
    }
    return contexts;
}

} // namespace

// =============================================================================================
// 1. activeContext() over synthetic focus chains -- D1-5, guide case A-53.
// =============================================================================================

TEST_CASE("every declarable key context is found on the widget that declares it", "[context]")
{
    // Over the enum rather than over a list written here. A context added to KeyContext.h and
    // forgotten by this test would otherwise be untested and look tested.
    for (const core::KeyContext context : declarableContexts()) {
        QWidget host;
        auto*   leaf = new QWidget(&host);
        ShortcutBinder::declareContext(&host, context);

        INFO(QString::fromLatin1(core::keyContextName(context).data()).toStdString());
        CHECK(ShortcutBinder::contextFor(leaf) == context);
        CHECK(ShortcutBinder::declaredContext(&host) == context);
    }
}

TEST_CASE("the innermost declaration wins over an outer one", "[context]")
{
    // The Comments pane inside a Viewer -- the nesting the guide names, and the reason the walk
    // goes outward from the focus rather than inward from the window. If it went the other way,
    // every pane in the product would answer Viewer and §8's per-pane tables would be dead text.
    QWidget viewer;
    auto*   comments = new QWidget(&viewer);
    auto*   list = new QWidget(comments);

    ShortcutBinder::declareContext(&viewer, core::KeyContext::Viewer);
    ShortcutBinder::declareContext(comments, core::KeyContext::Comments);

    CHECK(ShortcutBinder::contextFor(list) == core::KeyContext::Comments);
    CHECK(ShortcutBinder::contextFor(comments) == core::KeyContext::Comments);
    CHECK(ShortcutBinder::contextFor(&viewer) == core::KeyContext::Viewer);
}

TEST_CASE("a chain that declares nothing is Global", "[context]")
{
    // Which is the whole of the Stage 1 shell: no page surface, no panes, no Write mode. The
    // fallback is not a defect to be fixed later -- it is what makes every §8 Global default work
    // in a window that has no context-bearing surface in it yet.
    QWidget window;
    auto*   middle = new QWidget(&window);
    auto*   leaf = new QWidget(middle);

    CHECK(ShortcutBinder::contextFor(leaf) == core::KeyContext::Global);
    CHECK(ShortcutBinder::contextFor(nullptr) == core::KeyContext::Global);
}

TEST_CASE("a detached widget answers for itself and does not reach a former parent", "[context]")
{
    QWidget viewer;
    ShortcutBinder::declareContext(&viewer, core::KeyContext::Viewer);

    auto* pane = new QWidget(&viewer);
    ShortcutBinder::declareContext(pane, core::KeyContext::Bookmarks);
    CHECK(ShortcutBinder::contextFor(pane) == core::KeyContext::Bookmarks);

    // Torn out -- which T12 made an ordinary thing to do to a widget rather than a hypothetical.
    pane->setParent(nullptr);
    CHECK(ShortcutBinder::contextFor(pane) == core::KeyContext::Bookmarks);

    QWidget orphan;
    CHECK(ShortcutBinder::contextFor(&orphan) == core::KeyContext::Global);
    delete pane;
}

TEST_CASE("declaring Common is refused rather than stored", "[context]")
{
    // KeyContext.h states it: Common is a resolution LAYER, not a place the focus can be. A widget
    // that could claim it would make the innermost-then-Common-then-Global order resolve Common
    // twice and the real context never.
    QWidget widget;
    ShortcutBinder::declareContext(&widget, core::KeyContext::Common);

    CHECK_FALSE(ShortcutBinder::declaredContext(&widget).has_value());
    CHECK(ShortcutBinder::contextFor(&widget) == core::KeyContext::Global);
}

TEST_CASE("an unreadable context property is ignored and the walk continues", "[context]")
{
    // Somebody sets the property by hand, or a newer build writes a context this one does not
    // know. Falling through to the parent is right; asserting is not, because the value arrives
    // from data rather than from code.
    QWidget viewer;
    ShortcutBinder::declareContext(&viewer, core::KeyContext::Viewer);

    auto* confused = new QWidget(&viewer);
    confused->setProperty(ShortcutBinder::contextPropertyName(), QStringLiteral("Nowhere"));

    CHECK_FALSE(ShortcutBinder::declaredContext(confused).has_value());
    CHECK(ShortcutBinder::contextFor(confused) == core::KeyContext::Viewer);
}

TEST_CASE("activeContext follows the focus inside the binder's own window", "[context]")
{
    QWidget window;
    auto*   viewer = new QWidget(&window);
    auto*   leaf = new QWidget(viewer);
    leaf->setFocusPolicy(Qt::StrongFocus);
    ShortcutBinder::declareContext(viewer, core::KeyContext::Viewer);

    Fixture       fixture;
    ShortcutBinder binder(fixture.actions, fixture.keyBindings, &window);

    CHECK(binder.activeContext() == core::KeyContext::Global);   // nothing focused yet
    leaf->setFocus();
    CHECK(binder.activeContext() == core::KeyContext::Viewer);
}

// =============================================================================================
// 2. The QtGui boundary -- a key press becomes a Chord, or it becomes nothing.
// =============================================================================================

TEST_CASE("a key press becomes the chord Features section 8 spells", "[chord]")
{
    struct Case { Qt::Key key; Qt::KeyboardModifiers modifiers; const char* expected; };

    const Case cases[] = {
        {Qt::Key_O,        Qt::ControlModifier,                     "Ctrl+O"},
        {Qt::Key_S,        Qt::ControlModifier | Qt::ShiftModifier, "Ctrl+Shift+S"},
        {Qt::Key_F9,       Qt::NoModifier,                          "F9"},
        {Qt::Key_F11,      Qt::NoModifier,                          "F11"},
        {Qt::Key_Equal,    Qt::ControlModifier,                     "Ctrl+="},
        {Qt::Key_Minus,    Qt::ControlModifier,                     "Ctrl+-"},
        {Qt::Key_Space,    Qt::NoModifier,                          "Space"},
        {Qt::Key_PageDown, Qt::NoModifier,                          "Page Down"},
        {Qt::Key_Escape,   Qt::NoModifier,                          "Esc"},
        {Qt::Key_Enter,    Qt::AltModifier,                         "Alt+Enter"},   // the keypad's
        {Qt::Key_Alt,      Qt::AltModifier,                         "Alt"},         // the key tips
        {Qt::Key_Tab,      Qt::ControlModifier,                     "Ctrl+Tab"},
    };

    for (const Case& testCase : cases) {
        QKeyEvent event(QEvent::KeyPress, testCase.key, testCase.modifiers);
        const std::optional<core::Chord> chord = ShortcutBinder::chordFor(&event);
        INFO(testCase.expected);
        REQUIRE(chord.has_value());
        CHECK(chord->toString() == testCase.expected);
    }
}

TEST_CASE("Shift plus Tab arrives as Backtab and is still Shift plus Tab", "[chord]")
{
    // MEASURED RATHER THAN ASSUMED, and it is the one conversion that would have failed silently:
    // Qt does not deliver Shift+Tab as Tab at all, so Ctrl+Shift+Tab -- a chord §8 binds to
    // window.prevTab and a user presses constantly -- would have resolved to nothing while every
    // other Shift chord worked.
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
    const std::optional<core::Chord> chord = ShortcutBinder::chordFor(&event);
    REQUIRE(chord.has_value());
    CHECK(chord->toString() == "Ctrl+Shift+Tab");

    // And with the Shift modifier stripped -- some platforms report Backtab without it -- the
    // answer is the same chord rather than a Ctrl+Tab that would step the WRONG WAY.
    QKeyEvent stripped(QEvent::KeyPress, Qt::Key_Backtab, Qt::ControlModifier);
    const std::optional<core::Chord> fromStripped = ShortcutBinder::chordFor(&stripped);
    REQUIRE(fromStripped.has_value());
    CHECK(fromStripped->toString() == "Ctrl+Shift+Tab");
}

TEST_CASE("a key press that is not a chord becomes nothing rather than a wrong chord", "[chord]")
{
    struct Case { Qt::Key key; Qt::KeyboardModifiers modifiers; };
    const Case cases[] = {
        {Qt::Key_O, Qt::MetaModifier | Qt::ControlModifier},   // the Windows key is the OS's
        {Qt::Key_Control, Qt::ControlModifier},                // a modifier held on its own
        {Qt::Key_Shift, Qt::ShiftModifier},
        {Qt::Key_unknown, Qt::NoModifier},
    };

    for (const Case& testCase : cases) {
        QKeyEvent event(QEvent::KeyPress, testCase.key, testCase.modifiers);
        CHECK_FALSE(ShortcutBinder::chordFor(&event).has_value());
    }
    CHECK_FALSE(ShortcutBinder::chordFor(nullptr).has_value());
}

TEST_CASE("every binding in the tables can be produced by a keyboard", "[chord]")
{
    // THE CENSUS, AND IT IS THE CASE THAT COULD HAVE FOUND A WHOLE DEAD TABLE. A chord whose key
    // this class cannot name looks identical to a working one everywhere else: the store holds it,
    // the collision census counts it, the tool finder prints it. Only the keyboard disagrees.
    Fixture fixture;
    const QList<QString> unreachable =
        ShortcutBinder::unreachableBindings(fixture.keyBindings);
    for (const QString& row : unreachable) WARN(row.toStdString());
    CHECK(unreachable.isEmpty());

    // The round trip, which is the stronger statement: turned into a key event and read back,
    // every row is the SAME chord. `unreachableBindings` only says a key has a name.
    //
    // WHAT THIS DOES NOT COVER (convention 19): the event is built from the chord, so it is the
    // event this class would EXPECT, not the one a keyboard sends. `Ctrl+Shift+=` round-trips
    // here as `Key_Equal` + Shift, and Qt on Windows delivers `Key_Plus` + Shift -- S2 issue I-33,
    // dead in the product for its whole life while this loop was green. The case after this one
    // builds the event the way a keyboard does.
    for (const core::KeyBinding& binding : fixture.keyBindings.allBindings()) {
        INFO(chordText(binding.chord).toStdString());
        const std::optional<Qt::Key> key = ShortcutBinder::qtKeyFor(binding.chord);
        REQUIRE(key.has_value());

        QKeyEvent event(QEvent::KeyPress, *key, ShortcutBinder::qtModifiersFor(binding.chord));
        const std::optional<core::Chord> read = ShortcutBinder::chordFor(&event);
        REQUIRE(read.has_value());
        CHECK(*read == binding.chord);
    }
}

TEST_CASE("a shifted punctuation key offers the reported key then the unshifted one", "[chord]")
{
    // S2 issue I-33. The REPORTED keys below are measured, not assumed: a Qt probe window on
    // Windows 11, driven by the physical keyboard and by injected input alike, received
    // `Ctrl+Shift+=` as Key_Plus (0x2B) and `Ctrl+Shift+-` as Key_Underscore (0x5F), while
    // `Ctrl+Shift+N` stayed Key_N. The order is the contract `eventFilter` walks.
    struct Case {
        Qt::Key                  key;
        Qt::KeyboardModifiers    modifiers;
        std::vector<const char*> expected;
    };
    const Qt::KeyboardModifiers ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

    const Case cases[] = {
        {Qt::Key_Plus,       ctrlShift, {"Ctrl+Shift++", "Ctrl+Shift+=", "Ctrl++"}},
        {Qt::Key_Underscore, ctrlShift, {"Ctrl+Shift+_", "Ctrl+Shift+-", "Ctrl+_"}},
        {Qt::Key_Ampersand,  ctrlShift, {"Ctrl+Shift+&", "Ctrl+Shift+7", "Ctrl+&"}},
        {Qt::Key_Greater,    ctrlShift, {"Ctrl+Shift+>", "Ctrl+Shift+.", "Ctrl+>"}},
        {Qt::Key_Plus,       Qt::ShiftModifier, {"Shift++", "Shift+=", "+"}},
        // One candidate, each for its own reason: no Shift (the keypad's `+`), a letter, a key
        // with no shifted form, and Backtab, whose chord already carries Shift but names a word.
        {Qt::Key_Plus,       Qt::ControlModifier, {"Ctrl++"}},
        {Qt::Key_N,          ctrlShift, {"Ctrl+Shift+N"}},
        {Qt::Key_Equal,      Qt::ControlModifier, {"Ctrl+="}},
        {Qt::Key_Backtab,    ctrlShift, {"Ctrl+Shift+Tab"}},
    };

    for (const Case& testCase : cases) {
        QKeyEvent event(QEvent::KeyPress, testCase.key, testCase.modifiers);
        const std::vector<core::Chord> candidates = ShortcutBinder::chordCandidatesFor(&event);
        INFO(testCase.expected.front());
        REQUIRE(candidates.size() == testCase.expected.size());
        for (std::size_t i = 0; i < candidates.size(); ++i)
            CHECK(candidates[i].toString() == testCase.expected[i]);
    }

    // Nothing in, nothing out -- the same refusals as `chordFor`.
    QKeyEvent meta(QEvent::KeyPress, Qt::Key_Plus, Qt::MetaModifier | Qt::ShiftModifier);
    CHECK(ShortcutBinder::chordCandidatesFor(&meta).empty());
    CHECK(ShortcutBinder::chordCandidatesFor(nullptr).empty());
}

TEST_CASE("every shifted punctuation binding resolves from the key a keyboard sends", "[chord]")
{
    // THE CENSUS I-33 WAS MISSING, over the whole table rather than the two chords M-10 found:
    // `Ctrl+Shift+7` and `Ctrl+Shift+8` in Write were dead the same way and nobody had pressed
    // them. Each affected row's key press is built as a US keyboard delivers it -- the SHIFTED
    // character whenever Shift is down on the main block -- and resolved through the candidates
    // in the order `eventFilter` uses. The first candidate any row binds must be THIS row.
    //
    // A row spelled by the character with no Shift (`+`, `*`) is pressed with Shift, which is the
    // only way to type it without a numeric keypad; the keypad press is `chordFor`'s identity and
    // the round trip above already covers it.
    //
    // WHAT THIS DOES NOT COVER (convention 19): Qt's Windows key mapper itself. The event is still
    // constructed here, shaped by the probe measurement recorded at I-33; that the mapper keeps
    // delivering it that way is M-10's to see, by hand.
    const auto unshiftedOf = [](char c) -> std::optional<char> {
        static const std::string_view shifted   = "!@#$%^&*()_+{}|:\"<>?~";
        static const std::string_view unshifted = "1234567890-=[]\\;',./`";
        const std::size_t at = shifted.find(c);
        return at == std::string_view::npos ? std::nullopt : std::optional<char>(unshifted[at]);
    };
    const auto shiftedOf = [](char c) -> std::optional<char> {
        static const std::string_view shifted   = "!@#$%^&*()_+{}|:\"<>?~";
        static const std::string_view unshifted = "1234567890-=[]\\;',./`";
        const std::size_t at = unshifted.find(c);
        return at == std::string_view::npos ? std::nullopt : std::optional<char>(shifted[at]);
    };

    Fixture fixture;
    int affected = 0;
    for (const core::KeyBinding& binding : fixture.keyBindings.allBindings()) {
        if (binding.chord.key().size() != 1) continue;
        const char key = binding.chord.key().front();

        Qt::KeyboardModifiers modifiers = ShortcutBinder::qtModifiersFor(binding.chord);
        char delivered = 0;
        if (binding.chord.hasShift()) {
            const std::optional<char> shifted = shiftedOf(key);
            if (!shifted) continue;              // a letter, or already the character
            delivered = *shifted;
        } else if (unshiftedOf(key)) {
            delivered = key;                     // `+` typed as Shift+=
            modifiers |= Qt::ShiftModifier;
        } else {
            continue;
        }
        ++affected;

        const std::string row = binding.chord.toString() + " in "
                                + std::string(core::keyContextName(binding.context));
        INFO(row);
        QKeyEvent event(QEvent::KeyPress, static_cast<Qt::Key>(delivered), modifiers);
        std::optional<core::KeyBinding> resolved;
        for (const core::Chord& candidate : ShortcutBinder::chordCandidatesFor(&event)) {
            resolved = fixture.keyBindings.resolve(candidate, binding.context);
            if (resolved) break;
        }
        REQUIRE(resolved.has_value());
        CHECK(resolved->chord == binding.chord);
        CHECK(resolved->action == binding.action);
    }

    // Measured over `KeyBindingDefaults.def` on 2026-09-13: Ctrl+Shift+= and Ctrl+Shift+- in
    // Viewer; Ctrl+Shift+=, Ctrl+Shift+7 and Ctrl+Shift+8 in Write; `+` in PageThumbnails and
    // `*` in Bookmarks. A census that silently matched nothing would pass; this says it did not.
    CHECK(affected == 7);
}

// =============================================================================================
// 3. Resolution, dispatch, and what gets consumed.
// =============================================================================================

TEST_CASE("every Global default resolves to the action section 8 gives it", "[chord]")
{
    // THE T13 EXIT CRITERION, asserted over the table rather than over a handful of chords. Each
    // Global row is registered with a recording handler, the chord is pressed, and the id that
    // arrives is compared with the row's own -- so a resolution that returned the right KIND of
    // answer for the wrong action would fail here.
    Fixture fixture;
    QWidget scope;
    ShortcutBinder binder(fixture.actions, fixture.keyBindings, &scope);

    core::ActionId  dispatched = core::ActionId::FileNew;
    int             dispatchCount = 0;
    const auto      record = [&dispatched, &dispatchCount](core::ActionId id) {
        return [&dispatched, &dispatchCount, id](core::KeyContext) {
            dispatched = id;
            ++dispatchCount;
        };
    };

    const std::vector<core::KeyBinding> globals =
        fixture.keyBindings.bindingsFor(core::KeyContext::Global);
    REQUIRE(globals.size() > 20);   // §8's Global table, not an empty store answering vacuously

    for (const core::KeyBinding& binding : globals) {
        if (binding.kind != core::BindingKind::Dispatched) continue;
        REQUIRE(binding.action.has_value());
        if (!fixture.actions.isRegistered(*binding.action))
            REQUIRE(fixture.actions.register_(*binding.action, record(*binding.action)));
    }

    for (const core::KeyBinding& binding : globals) {
        if (binding.kind != core::BindingKind::Dispatched) continue;

        const int before = dispatchCount;
        INFO(chordText(binding.chord).toStdString());
        CHECK(binder.activate(binding.chord, core::KeyContext::Global)
              == ShortcutBinder::Outcome::Dispatched);
        CHECK(dispatchCount == before + 1);
        CHECK(dispatched == *binding.action);
    }
}

TEST_CASE("a chord resolves innermost context first then Common then Global", "[chord]")
{
    // The same sweep over EVERY context's own rows. Resolution order is the model -- it is what
    // lets Ctrl+K mean Preferences in the Viewer and Insert-hyperlink in Write mode -- so a row
    // that resolved to its Global or Common neighbour instead would be the model quietly gone.
    Fixture fixture;
    QWidget scope;
    ShortcutBinder binder(fixture.actions, fixture.keyBindings, &scope);

    core::ActionId dispatched = core::ActionId::FileNew;
    for (const core::KeyBinding& binding : fixture.keyBindings.allBindings()) {
        if (binding.kind != core::BindingKind::Dispatched) continue;
        if (fixture.actions.isRegistered(*binding.action)) continue;
        const core::ActionId id = *binding.action;
        REQUIRE(fixture.actions.register_(
            id, [&dispatched, id](core::KeyContext) { dispatched = id; }));
    }

    for (const core::KeyBinding& binding : fixture.keyBindings.allBindings()) {
        if (binding.kind != core::BindingKind::Dispatched) continue;
        if (binding.context == core::KeyContext::Common) continue;   // has no context of its own

        INFO(chordText(binding.chord).toStdString());
        REQUIRE(binder.activate(binding.chord, binding.context)
                == ShortcutBinder::Outcome::Dispatched);
        CHECK(dispatched == *binding.action);
    }
}

TEST_CASE("the five outcomes are distinguished and only two consume the key", "[chord]")
{
    Fixture fixture;
    QWidget scope;
    ShortcutBinder binder(fixture.actions, fixture.keyBindings, &scope);

    bool ran = false;
    REQUIRE(fixture.actions.register_(core::ActionId::ViewToggleSidebar,
                                      [&ran](core::KeyContext) { ran = true; }));
    REQUIRE(fixture.actions.register_(
        core::ActionId::FileClose, [](core::KeyContext) {}, [] { return false; }));

    const core::Chord f4 = *core::Chord::parse("F4");                 // -> view.toggleSidebar
    const core::Chord ctrlW = *core::Chord::parse("Ctrl+W");          // registered, disabled
    const core::Chord ctrlS = *core::Chord::parse("Ctrl+S");          // declared, unregistered
    const core::Chord up = *core::Chord::parse("Up");                 // widget-native in Viewer
    const core::Chord unbound = *core::Chord::parse("Ctrl+Alt+J");    // §8 binds nothing

    CHECK(binder.activate(f4, core::KeyContext::Global) == ShortcutBinder::Outcome::Dispatched);
    CHECK(ran);
    CHECK(binder.activate(ctrlW, core::KeyContext::Global) == ShortcutBinder::Outcome::Unavailable);
    CHECK(binder.activate(ctrlS, core::KeyContext::Global) == ShortcutBinder::Outcome::NotImplemented);
    CHECK(binder.activate(up, core::KeyContext::Viewer) == ShortcutBinder::Outcome::WidgetNative);
    CHECK(binder.activate(unbound, core::KeyContext::Global) == ShortcutBinder::Outcome::NoBinding);

    // AND WHAT EACH OF THEM DOES TO THE KEY EVENT, which is the half a user notices. `Unavailable`
    // consumes: the command answered "not right now", and letting Qt then move the focus instead
    // would make Ctrl+Tab mean two different things depending on how many tabs are open.
    // `NotImplemented` does not: Stage 1 registers seventeen of 358 ids, and eating the other
    // ~341 chords would make the shell swallow Ctrl+S for the next twelve stages.
    CHECK(ShortcutBinder::consumes(ShortcutBinder::Outcome::Dispatched));
    CHECK(ShortcutBinder::consumes(ShortcutBinder::Outcome::Unavailable));
    CHECK_FALSE(ShortcutBinder::consumes(ShortcutBinder::Outcome::NotImplemented));
    CHECK_FALSE(ShortcutBinder::consumes(ShortcutBinder::Outcome::WidgetNative));
    CHECK_FALSE(ShortcutBinder::consumes(ShortcutBinder::Outcome::NoBinding));
}

TEST_CASE("a user remap takes effect without anything being rebuilt", "[chord]")
{
    // The reason chords are not QAction shortcuts. The store holds the override, the binder reads
    // it at the moment the key is pressed, and no widget knew anything happened.
    Fixture fixture;
    QWidget scope;
    ShortcutBinder binder(fixture.actions, fixture.keyBindings, &scope);

    bool toggled = false;
    REQUIRE(fixture.actions.register_(core::ActionId::ViewToggleSidebar,
                                      [&toggled](core::KeyContext) { toggled = true; }));

    const core::Chord f7 = *core::Chord::parse("F7");
    REQUIRE(fixture.keyBindings.rebind(f7, core::KeyContext::Global,
                                       core::ActionId::ViewToggleSidebar));

    CHECK(binder.activate(f7, core::KeyContext::Global) == ShortcutBinder::Outcome::Dispatched);
    CHECK(toggled);
}

// =============================================================================================
// The window, end to end -- a key press reaching a command.
// =============================================================================================

TEST_CASE("a key press on the window dispatches through the registry", "[chord]")
{
    Fixture fixture;
    MainWindow window(fixture.services());
    window.show();

    REQUIRE(window.sidebar()->isVisible());

    // F4 is §8's Global chord for view.toggleSidebar, and the command it reaches is the same one
    // the ribbon button reaches. Nothing about this window changed at T13 except that the key now
    // arrives -- which is what "the key, not the behaviour" means.
    QKeyEvent press(QEvent::KeyPress, Qt::Key_F4, Qt::NoModifier);
    press.ignore();
    QCoreApplication::sendEvent(&window, &press);
    CHECK_FALSE(window.sidebar()->isVisible());
    CHECK(press.isAccepted());   // consumed -- nothing else gets to interpret it

    QKeyEvent again(QEvent::KeyPress, Qt::Key_F4, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &again);
    CHECK(window.sidebar()->isVisible());
}

TEST_CASE("a chord whose command is not registered yet leaves the key alone", "[chord]")
{
    Fixture fixture;
    MainWindow window(fixture.services());
    window.show();

    // Ctrl+P is file.print: declared, and not registered as of Stage 3 T10. The event must come back
    // UNACCEPTED, so whatever else would have handled it still can.
    //
    // MOVED, NOT DELETED (Stage 3 guide case A-81). This case pressed Ctrl+S until Stage 3 T10 registered
    // `file.save`; a registered command that is refused CONSUMES its key (`ShortcutBinder::consumes`), so
    // Ctrl+S no longer says anything about an unregistered one. What the case proves is unchanged.
    REQUIRE_FALSE(fixture.actions.isRegistered(core::ActionId::FilePrint));
    QKeyEvent press(QEvent::KeyPress, Qt::Key_P, Qt::ControlModifier);
    press.ignore();
    QCoreApplication::sendEvent(&window, &press);
    CHECK_FALSE(press.isAccepted());
}

TEST_CASE("a text field keeps the keys it edits with", "[chord]")
{
    // Ctrl+A is edit.selectAll in §8's Common table AND what a line edit does with its own text.
    // The binder asks the focused widget through Qt's own ShortcutOverride handshake, so this is
    // correct for widgets nobody has written yet -- there is no list of text widgets to maintain.
    Fixture fixture;
    MainWindow window(fixture.services());
    window.show();

    // REGISTERED FOR THIS CASE, because nothing registers `edit.selectAll` as of Stage 3 T10 and a test of
    // a collision needs one. Whichever task registers it for real is the moment that would otherwise break
    // every line edit in the product -- and the REQUIRE below fails that day, so this line is revisited.
    bool selectAllDispatched = false;
    REQUIRE(fixture.actions.register_(
        core::ActionId::EditSelectAll,
        [&selectAllDispatched](core::KeyContext) { selectAllDispatched = true; }));

    auto* field = new QLineEdit(&window);
    field->setText(QStringLiteral("select me"));
    field->show();
    field->setFocus();
    REQUIRE(window.focusWidget() == field);

    QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    press.ignore();
    QCoreApplication::sendEvent(field, &press);
    CHECK_FALSE(selectAllDispatched);                     // the binder stood aside
    CHECK(field->selectedText() == field->text());        // and the field did its own job

    // And a chord the field does NOT implement still reaches the shell from inside it.
    bool sidebarWasVisible = window.sidebar()->isVisible();
    QKeyEvent shellChord(QEvent::KeyPress, Qt::Key_F4, Qt::NoModifier);
    QCoreApplication::sendEvent(field, &shellChord);
    CHECK(window.sidebar()->isVisible() != sidebarWasVisible);
}

TEST_CASE("a second window does not dispatch the first window's key", "[chord]")
{
    // One binder per window, filtering the application: the check that a chord typed into window B
    // is handled once, by B. Without the window test in `eventFilter` the sidebar would toggle
    // twice and end up exactly where it started -- a bug that looks like nothing happening.
    Fixture fixture;
    MainWindow first(fixture.services());
    MainWindow second(fixture.services());
    first.show();
    second.show();

    const bool firstBefore = first.sidebar()->isVisible();
    REQUIRE(second.sidebar()->isVisible());

    second.raiseToForeground();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_F4, Qt::NoModifier);
    QCoreApplication::sendEvent(&second, &press);

    CHECK_FALSE(second.sidebar()->isVisible());
    CHECK(first.sidebar()->isVisible() == firstBefore);
}

// =============================================================================================
// The self-check -- T13's half of D1-7, and it is planted the way A-54's is.
// =============================================================================================

TEST_CASE("the real window carries no shortcut that bypasses the binder", "[chord]")
{
    // The positive control, and it is the product rather than a fixture -- I-18's lesson: a
    // self-check that has only ever met fixtures has not met its subject.
    Fixture fixture;
    MainWindow window(fixture.services());
    window.show();

    const QList<QString> complaints = ShortcutBinder::shortcutComplaints(&window);
    for (const QString& complaint : complaints) WARN(complaint.toStdString());
    CHECK(complaints.isEmpty());
}

TEST_CASE("a planted QAction shortcut is named as a bypass", "[chord]")
{
    // KIND ONE. Nothing in the product may do this, and no lint can see it: `setShortcut` on an
    // action ActionFactory legitimately built is one legal line that removes the chord from the
    // context stack and from the user's remap at the same time.
    QWidget window;
    auto*   action = new QAction(&window);
    action->setObjectName(QStringLiteral("smuggled"));
    action->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));

    const QList<QString> complaints = ShortcutBinder::shortcutComplaints(&window);
    REQUIRE(complaints.size() == 1);
    CHECK(complaints.first().contains(QStringLiteral("smuggled")));
    CHECK(complaints.first().contains(QStringLiteral("Ctrl+G")));
    CHECK_FALSE(ShortcutBinder::shortcutsAreSound(&window));
}

TEST_CASE("a planted QShortcut is named separately", "[chord]")
{
    // KIND TWO, named separately because the fix is different: an action needs its sequence
    // removed, a QShortcut needs deleting and its behaviour registering as a command.
    QWidget window;
    auto*   shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+G")), &window);
    Q_UNUSED(shortcut);

    const QList<QString> complaints = ShortcutBinder::shortcutComplaints(&window);
    REQUIRE(complaints.size() == 1);
    CHECK(complaints.first().contains(QStringLiteral("QShortcut")));
}

// =============================================================================================
// 5. KeyContext::Viewer -- the first context the product has ever actually resolved to.
//    Stage 2 T13, decision D2-8. Guide cases A-55 and A-56.
// =============================================================================================

namespace {

// A file the fake backend accepts. Its bytes are irrelevant here: `tests/ui` links no engine.
QString writeOpenable(const QTemporaryDir& directory, const QString& name)
{
    const QString path = QDir(directory.path()).filePath(name);
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("the fake backend decides, not these bytes");
    file.close();
    return QFileInfo(path).absoluteFilePath();
}

} // namespace

TEST_CASE("focus inside the page view resolves to Viewer, and nowhere else does", "[context]")
{
    // A-55, AND IT IS A FIRST. Stage 1 shipped the whole context-resolution mechanism with nothing
    // declaring a context, so `activeContext()` answered `Global` for every keystroke the product
    // had ever seen -- correct then, and completely untested against a real second context. Note
    // the consequence for a regression: this answering `Global` again would look exactly like the
    // previous fifteen tasks' correct behaviour.
    Fixture fixture;
    MainWindow window(fixture.services());
    window.resize(1000, 800);
    window.show();

    window.openFiles({writeOpenable(fixture.directory, QStringLiteral("viewer.pdf"))});
    REQUIRE(window.documentCount() == 1);
    QCoreApplication::processEvents();

    ui::DocumentWindow* document = window.currentDocument();
    REQUIRE(document != nullptr);
    ui::PageView* view = document->view();
    REQUIRE(view != nullptr);

    ShortcutBinder binder(fixture.actions, fixture.keyBindings, &window);

    // The chrome is still Global -- the ribbon, the toolbar, the tab strip. Only the page surface
    // is Viewer, and asserting the outside is what makes the inside mean something.
    window.ribbon()->setFocus();
    CHECK(binder.activeContext() == core::KeyContext::Global);

    view->setFocus();
    CHECK(binder.activeContext() == core::KeyContext::Viewer);
    CHECK(ShortcutBinder::contextFor(view) == core::KeyContext::Viewer);

    // The declaration is on the view itself, so anything Stage 4 puts INSIDE it -- an overlay, a
    // ruler, a tool's own widget -- inherits Viewer without having to remember to say so.
    auto* child = new QWidget(view);
    child->setFocusPolicy(Qt::StrongFocus);
    child->setFocus();
    CHECK(binder.activeContext() == core::KeyContext::Viewer);
}

TEST_CASE("a Viewer chord does not fire from the tool finder's search box", "[context]")
{
    // A-56, and it is the case that actually tests context resolution: a chord that fired
    // everywhere would pass A-55 and be wrong. `H` is `tool.hand` in §8's Viewer table and a
    // perfectly ordinary letter in a text box, which is exactly the collision the twelve
    // deliberate ones in `Features.md` §8 are made of.
    Fixture fixture;
    MainWindow window(fixture.services());
    window.resize(1000, 800);
    window.show();

    window.openFiles({writeOpenable(fixture.directory, QStringLiteral("hand.pdf"))});
    QCoreApplication::processEvents();

    ui::PageView* view = window.currentDocument()->view();
    REQUIRE(view != nullptr);

    // THE PRODUCT'S OWN REGISTRATION, as of T14. This case registered `ToolHand` itself at T13,
    // saying at the assertion that it had to because nothing did -- and T14 registering it for
    // real is what turns the case from a test of the ROUTE into a test of the route AND its
    // effect. The registration is now asserted rather than performed: a case that re-registered
    // would be refused (property 3) and, worse, would be observing its own lambda again.
    REQUIRE(fixture.actions.isRegistered(core::ActionId::ToolHand));
    REQUIRE(view->tool() == ui::PageView::Tool::Select);

    ShortcutBinder binder(fixture.actions, fixture.keyBindings, &window);
    const std::optional<core::Chord> h = core::Chord::parse("H");
    REQUIRE(h.has_value());

    // In the viewer it dispatches, AND THE VIEWER CHANGES -- the observable effect rather than a
    // counter, which is what distinguishes "the command ran" from "the command did something".
    view->setFocus();
    REQUIRE(binder.activeContext() == core::KeyContext::Viewer);
    CHECK(binder.activate(*h) == ShortcutBinder::Outcome::Dispatched);
    CHECK(view->tool() == ui::PageView::Tool::Hand);

    // IN THE SEARCH BOX IT DOES NOT. `H` has no Global and no Common row, so the resolution finds
    // nothing and the key is deliberately left unconsumed -- which is what lets the line edit type
    // an `h`.
    view->setTool(ui::PageView::Tool::Select);
    window.toolFinder()->searchBox()->setFocus();
    REQUIRE(binder.activeContext() == core::KeyContext::Global);
    CHECK(binder.activate(*h) == ShortcutBinder::Outcome::NoBinding);
    CHECK_FALSE(ShortcutBinder::consumes(binder.activate(*h)));
    CHECK(view->tool() == ui::PageView::Tool::Select);

    // AND THE KEY REALLY REACHES THE FIELD. Stage 1's I-54 is the warning attached to this case:
    // that defect existed because every tool-finder case typed at `searchBox()`, the one address a
    // real keystroke does not reach while the result list is up. Here the event is sent to the
    // WINDOW, which is where a keystroke actually arrives, and the field is asked what it holds.
    QLineEdit* box = window.toolFinder()->searchBox();
    box->clear();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
    press.ignore();
    QCoreApplication::sendEvent(&window, &press);
    CHECK_FALSE(press.isAccepted());
    CHECK(view->tool() == ui::PageView::Tool::Select);
}
