// The keyboard's one entrance -- Stage 1 T13.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS CLASS IS FOR, AND WHY IT IS NOT `QAction::setShortcut`.
//
// Qt has a perfectly good shortcut system, and using it would have been half a day's work: give
// every `RawPageAction` its chord and let `QShortcutMap` do the rest. It is the wrong mechanism
// here for one reason that Stage 1 exists to protect -- `Features.md` §8 binds a chord PER
// CONTEXT, and twelve of those bindings deliberately collide, so `Ctrl+K` means Preferences in the
// Viewer and Insert-hyperlink in Write mode. Qt resolves a shortcut against a widget's *ambiguity*
// rules, not against a context stack it knows nothing about, and there is no place in
// `QKeySequence` to say "innermost context, then Common, then Global". That order IS the model
// (`core/cmd/KeyBindingStore.h` says so in its own header), and a mechanism that cannot express it
// would quietly make §8 a document about something the product does not do.
//
// The second reason is the one the whole action layer is built on: a user REMAPS chords, and a
// remap has to take effect without rebuilding a widget. `KeyBindingStore` already holds the
// per-user override and the settings file already persists it (D1-10's `shortcuts/` family);
// pushing that state into 358 `QAction`s would give the store a shadow copy to keep in step with.
//
// So chord input arrives here, is resolved through the store, and is dispatched through
// `ActionRegistry` -- the same call a ribbon button, the tool finder, the CLI and the Stage 14
// batch runner make. That is what makes T13 "the key, not the behaviour": every command it can
// reach was already reachable, and this class adds no second implementation of any of them.
//
// ---------------------------------------------------------------------------------------------
// THREE HALVES, AND EACH IS SEPARATELY TESTABLE. (Three, because the middle one is a boundary.)
//
//   1. `activeContext()` -- a pure function of the focus chain. D1-5 puts it in `ui/` rather than
//      `core/` because a focus chain is made of `QWidget`s; guide case A-53 tests it against
//      synthetic, never-shown trees, which is why it is `static` and takes the widget.
//
//   2. `chordFor()` -- the QtGui/`core/` boundary. `core/cmd/Chord` exists precisely because
//      `QKeySequence` is QtGui and `core/` may not name it, and THIS is the file its header
//      promised would do the conversion. One table drives both directions, so the census below
//      can answer "which §8 chord could a keyboard never produce" from the same rows that produce
//      them -- the D1-6 habit at a smaller scale.
//
//   3. `activate()` -- resolve, then dispatch, then decide whether the key was consumed.
//
// ---------------------------------------------------------------------------------------------
// WHO GETS THE KEY FIRST: THE FOCUSED WIDGET, THROUGH QT'S OWN HANDSHAKE.
//
// `Ctrl+A` is `edit.selectAll` in §8's Common table, and it is also what a `QLineEdit` does with
// its own text. A shortcut layer that swallowed it would break every text field in the product,
// and a hard-coded list of "widgets that edit text" would be wrong the first time somebody adds
// one. Qt already solved this: before delivering a key press it offers the focused widget a
// `QEvent::ShortcutOverride`, and a widget that implements a key ACCEPTS it. `QLineEdit`,
// `QTextEdit` and `QAbstractSpinBox` all do. So this class asks the same question with the same
// event, and defers to whoever answers yes -- no list, and correct for widgets written later.
//
// ---------------------------------------------------------------------------------------------
// WHAT IT CONSUMES, WHICH IS NOT THE SAME AS WHAT IT RECOGNISES.
//
// A chord that dispatches is consumed. A chord refused as `Unavailable` -- registered, but not
// applicable right now, such as `Ctrl+Tab` in a window with one tab -- is ALSO consumed: the
// command exists and has answered, and letting Qt then move the focus instead would make one
// keystroke mean two different things depending on document state. A chord whose action is
// declared but not registered (`NotFound`) is NOT consumed: Stage 1 registers seventeen of 358
// ids, so most of §8 lands there today, and eating those keys would make the shell silently
// swallow `Ctrl+S` for the next twelve stages instead of letting the platform do whatever it
// would otherwise do with it.
//
// ---------------------------------------------------------------------------------------------
// ONE BINDER PER WINDOW, FILTERING THE APPLICATION.
//
// The filter is installed on `qApp`, because that is the only object that sees a key event before
// the focused widget does. But a second `MainWindow` (T12 tears a tab out into one) would then
// have a second binder seeing the same event, and the chord would dispatch twice. So each binder
// answers only for events delivered inside ITS OWN window -- `widget->window() == scope`. Exactly
// one binder matches any given key event, and the one that matches is the window the key was
// typed into, which is also the window `MainWindow::activeShell()` will name.

#ifndef RAWPAGE_UI_SHELL_SHORTCUTBINDER_H
#define RAWPAGE_UI_SHELL_SHORTCUTBINDER_H

#include "core/cmd/Chord.h"
#include "core/cmd/KeyContext.h"

#include <QList>
#include <QObject>
#include <QString>

#include <optional>
#include <vector>

class QKeyEvent;
class QWidget;

namespace rawpage::core {
class ActionRegistry;
class KeyBindingStore;
}

namespace rawpage::ui {

class ShortcutBinder : public QObject {
    Q_OBJECT

public:
    // `scope` is the window this binder answers for, and it is also the parent: a binder outliving
    // its window would go on filtering for a window that no longer exists.
    ShortcutBinder(core::ActionRegistry& registry,
                   const core::KeyBindingStore& bindings,
                   QWidget* scope);
    ~ShortcutBinder() override;

    // ---------------------------------------------------------------------------------------
    // 1. THE FOCUS CHAIN -- D1-5, guide case A-53.
    // ---------------------------------------------------------------------------------------
    // A widget says which §8 context it IS, once, and the innermost such widget above the focus
    // wins. Nothing in Stage 1 declares one: the Viewer surface is Stage 2, the panes are Stage 4
    // and later, and Write mode is Stage 13 -- so `activeContext()` answers `Global` throughout
    // the shell today, which is the honest answer rather than a gap. The mechanism is here, and
    // tested, because the alternative is Stage 2 inventing it in a hurry inside a page surface.
    //
    // `Common` is REFUSED. `core/cmd/KeyContext.h` says why in its own header: Common is a
    // resolution layer, not a place the focus can be, and a widget claiming to be it would make
    // `activeContext()` return a value the resolution order has no room for. `Global` is allowed,
    // and means "the layers only here" -- useful for an island inside a context's subtree.
    static void declareContext(QWidget* widget, core::KeyContext context);
    static std::optional<core::KeyContext> declaredContext(const QWidget* widget);

    // The pure function. Walks `parentWidget()` from `focus` upward; `Global` if nothing declares.
    // Static and taking the widget, so a test needs a parented tree and not a window.
    static core::KeyContext contextFor(const QWidget* focus);

    // `contextFor()` over this window's focus widget. This is what `ActionFactory` calls to
    // attribute a ribbon click, and what `eventFilter` calls to resolve a chord.
    core::KeyContext activeContext() const;

    // The dynamic-property name `declareContext` writes. Exposed so a test asserts against the
    // real marker rather than against a second copy of its spelling.
    static const char* contextPropertyName();

    // ---------------------------------------------------------------------------------------
    // 2. THE BOUNDARY -- QtGui stops here.
    // ---------------------------------------------------------------------------------------
    // Returns nothing for a key press that is not a chord §8 could contain: a bare Ctrl or Shift,
    // anything carrying the Meta (Windows) modifier, a dead key. Nothing rather than a wrong
    // chord, for the same reason `Chord::parse` returns nothing for `Ctrl+`arrows``.
    static std::optional<core::Chord> chordFor(const QKeyEvent* event);

    // The same conversion from a key and its modifiers rather than from an event. T14's Shortcuts
    // page reads a chord out of a `QKeySequenceEdit`, which reports a `QKeyCombination` and never
    // a `QKeyEvent`; synthesizing an event to feed the overload above would be manufacturing the
    // very thing this function exists to interpret. The event overload delegates here, so there is
    // one implementation and the two cannot disagree.
    static std::optional<core::Chord> chordFor(Qt::Key key, Qt::KeyboardModifiers modifiers);

    // EVERY CHORD ONE KEY PRESS COULD MEAN, in the order `eventFilter` tries them -- S2 issue
    // I-33. With Shift held, Qt reports the CHARACTER a punctuation key produces, not the key:
    // `Ctrl+Shift+=` arrives as `Key_Plus`, `Ctrl+Shift+7` as `Key_Ampersand` (measured on Windows
    // with a key-event probe, physical keys and injected ones alike). §8 spells those rows by the
    // key, but it spells `Ctrl+Shift+>` and a bare `+` by the character -- both conventions are in
    // the table, so neither reading alone can be right. Three candidates, Qt's own shortcut map's
    // rule: the chord as reported, the same modifiers on the UNSHIFTED key, and the reported
    // character with Shift removed. A key with no shifted form yields exactly one, `chordFor`'s.
    //
    // THE UNSHIFTED KEY IS A US KEYBOARD'S. Nothing here reads the active layout, so on a layout
    // whose shifted punctuation differs -- German, French -- the second candidate names the wrong
    // key; the first and third are unaffected. `chordFor` above stays the LITERAL reading, which
    // is what the Shortcuts page records.
    static std::vector<core::Chord> chordCandidatesFor(const QKeyEvent* event);
    static std::vector<core::Chord> chordCandidatesFor(Qt::Key key, Qt::KeyboardModifiers modifiers);

    // THE INVERSE, over the same table. Two callers, and neither of them is decoration: the census
    // below, and T14's Shortcuts page, which has to SHOW a chord as a key and read one back.
    // Returns nothing for a chord no keyboard can produce.
    static std::optional<Qt::Key>   qtKeyFor(const core::Chord& chord);
    static Qt::KeyboardModifiers    qtModifiersFor(const core::Chord& chord);

    // Which §8 rows name a key this class could never produce from a keyboard -- the difference
    // between "the bindings are loaded" and "the bindings can be typed". A row that appears here is
    // a binding the user cannot reach no matter what they press, and it would look exactly like a
    // working one in every other test: the store has it, the census counts it, `--list-actions`
    // prints it. Only the keyboard would disagree.
    static QList<QString> unreachableBindings(const core::KeyBindingStore& bindings);

    // ---------------------------------------------------------------------------------------
    // 3. RESOLUTION AND DISPATCH.
    // ---------------------------------------------------------------------------------------
    // Five outcomes, because collapsing any two of them loses something a caller needs. See the
    // header's consumption rule: the first two consume the key, the last three do not.
    enum class Outcome {
        Dispatched,      // resolved, registered, handled
        Unavailable,     // resolved and registered, refused for the current state
        NotImplemented,  // resolved to an id nothing has registered -- most of §8 at Stage 1
        WidgetNative,    // §8 records the row and says the focused widget owns it
        NoBinding,       // no §8 row for this chord in this context, nor in Common, nor in Global
    };

    Outcome activate(const core::Chord& chord, core::KeyContext context) const;
    Outcome activate(const core::Chord& chord) const { return activate(chord, activeContext()); }

    static bool consumes(Outcome outcome)
    {
        return outcome == Outcome::Dispatched || outcome == Outcome::Unavailable;
    }

    bool eventFilter(QObject* watched, QEvent* event) override;

    // ---------------------------------------------------------------------------------------
    // THE SELF-CHECK -- T13's half of D1-7, and the same shape as `ActionFactory`'s.
    // ---------------------------------------------------------------------------------------
    // An action carrying its own `QShortcut` is the T13-shaped version of the direct connection
    // D1-7 forbids: the key works, it looks correct, and it has bypassed the context stack and the
    // user's remap on its way. `ActionFactory` never sets one -- this reports anyone who did.
    //
    // Returns the complaints rather than asserting, for the reasons `ActionFactory` gives: a test
    // has to see the finding without the process dying, and the caller picks the severity.
    static QList<QString> shortcutComplaints(const QWidget* root);
    static bool shortcutsAreSound(const QWidget* root) { return shortcutComplaints(root).isEmpty(); }

private:
    core::ActionRegistry&        registry_;
    const core::KeyBindingStore& bindings_;
    QWidget*                     scope_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_SHORTCUTBINDER_H
